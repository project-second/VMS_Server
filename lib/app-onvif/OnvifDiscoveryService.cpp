#include "OnvifDiscoveryService.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "IntervalTimer.h"
#include "LogSync.h"

#include "wsddapi.h"
#include "wsaapi.h"

#include "wsdd.nsmap"

// gSOAP callback 함수는 free function이어야 하므로 private 상태 변경은 이 helper를 통해 제한적으로 연다.
struct OnvifDiscoveryEventAccess {
    static void upsert(OnvifDiscoveryService& service, OnvifDiscoveredDevice device) {
        service.upsertDevice(std::move(device));
    }

    static void markOffline(OnvifDiscoveryService& service, const std::string& endpointReference) {
        service.markDeviceOffline(endpointReference);
    }
};

namespace {

constexpr const char* kDiscoveryEndpoint = "soap.udp://239.255.255.250:3702";
constexpr const char* kOnvifNetworkVideoTransmitter = "\"http://www.onvif.org/ver10/network/wsdl\":NetworkVideoTransmitter";

// 하나의 gSOAP callback set을 active Probe, Resolve, passive listener가 공유한다.
// soap->user에 이 상태를 넣어 callback이 어느 흐름에서 호출됐는지 구분한다.
enum class WsddStateKind {
    Probe,
    Resolve,
    PassiveListener,
};

struct WsddEventState {
    WsddStateKind kind;
    OnvifDiscoveryService* service = nullptr;
    std::string messageId;
    std::vector<OnvifDiscoveredDevice> devices;
    std::optional<OnvifDiscoveredDevice> resolvedDevice;
};

std::vector<std::string> splitWhitespace(const char* value) {
    std::vector<std::string> items;
    if (value == nullptr) {
        return items;
    }

    std::istringstream stream(value);
    std::string item;
    while (stream >> item) {
        items.push_back(item);
    }
    return items;
}

std::string copyString(const char* value) {
    return value == nullptr ? std::string{} : std::string(value);
}

// EndpointReference가 우선 식별자이고, 비어 있으면 XAddrs 중복으로 같은 장비를 판단한다.
bool sameDeviceAddress(const OnvifDiscoveredDevice& left, const OnvifDiscoveredDevice& right) {
    if (!left.endpointReference.empty() && left.endpointReference == right.endpointReference) {
        return true;
    }

    for (const std::string& lhs : left.xaddrs) {
        for (const std::string& rhs : right.xaddrs) {
            if (!lhs.empty() && lhs == rhs) {
                return true;
            }
        }
    }

    return false;
}

// Hello/Probe/Resolve 응답은 일부 field만 채울 수 있어 비어 있지 않은 값만 갱신한다.
void mergeDeviceFields(OnvifDiscoveredDevice& current, const OnvifDiscoveredDevice& incoming) {
    if (!incoming.endpointReference.empty()) {
        current.endpointReference = incoming.endpointReference;
    }
    if (!incoming.types.empty()) {
        current.types = incoming.types;
    }
    if (!incoming.scopes.empty()) {
        current.scopes = incoming.scopes;
    }
    if (!incoming.xaddrs.empty()) {
        current.xaddrs = incoming.xaddrs;
    }
    if (incoming.metadataVersion != 0) {
        current.metadataVersion = incoming.metadataVersion;
    }
    current.online = incoming.online;
}

void addOrMergeDevice(std::vector<OnvifDiscoveredDevice>& devices, OnvifDiscoveredDevice device) {
    const auto it = std::find_if(devices.begin(), devices.end(), [&](const OnvifDiscoveredDevice& current) {
        return sameDeviceAddress(current, device);
    });

    if (it == devices.end()) {
        devices.push_back(std::move(device));
        return;
    }

    mergeDeviceFields(*it, device);
}

// soap_wsdd_listen은 음수 값을 microseconds timeout으로 해석한다.
int listenTimeoutMicros(std::chrono::milliseconds timeout) {
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(timeout).count();
    if (micros <= 0) {
        return -1000;
    }
    if (micros > 2147483647LL) {
        return -2147483647;
    }
    return -static_cast<int>(micros);
}

// Discovery는 UDP multicast 기반이다. TTL 1은 local network 밖으로 나가지 않게 한다.
void configureSoapContext(soap* context) {
    soap_set_namespaces(context, namespaces);
    context->connect_flags = SO_BROADCAST;
    context->ipv4_multicast_ttl = 1;
    context->recv_timeout = 3;
    context->send_timeout = 3;
    context->connect_timeout = 3;
}

std::unique_ptr<soap, void (*)(soap*)> makeSoapContext() {
    soap* context = soap_new1(SOAP_IO_UDP);
    configureSoapContext(context);
    return {context, [](soap* item) {
        if (item == nullptr) {
            return;
        }
        soap_destroy(item);
        soap_end(item);
        soap_free(item);
    }};
}

WsddEventState* stateFrom(soap* context) {
    return context == nullptr ? nullptr : static_cast<WsddEventState*>(context->user);
}

OnvifDiscoveredDevice deviceFromFields(const char* endpointReference, const char* types,
    const char* scopes, const char* xaddrs, unsigned int metadataVersion, bool online) {
    OnvifDiscoveredDevice device;
    device.endpointReference = copyString(endpointReference);
    device.types = copyString(types);
    device.scopes = copyString(scopes);
    device.xaddrs = splitWhitespace(xaddrs);
    device.metadataVersion = metadataVersion;
    device.online = online;
    return device;
}

OnvifDiscoveredDevice deviceFromProbeMatch(const wsdd__ProbeMatchType& match) {
    return deviceFromFields(
        match.wsa5__EndpointReference.Address,
        match.Types,
        match.Scopes == nullptr ? nullptr : match.Scopes->__item,
        match.XAddrs,
        match.MetadataVersion,
        true);
}

OnvifDiscoveredDevice deviceFromResolveMatch(const wsdd__ResolveMatchType& match) {
    return deviceFromFields(
        match.wsa5__EndpointReference.Address,
        match.Types,
        match.Scopes == nullptr ? nullptr : match.Scopes->__item,
        match.XAddrs,
        match.MetadataVersion,
        true);
}

// Passive listener는 서비스의 공유 상태를 바로 갱신하고, active 호출은 local 결과에 모은다.
void captureDevice(WsddEventState& state, OnvifDiscoveredDevice device) {
    if (state.service != nullptr && state.kind == WsddStateKind::PassiveListener) {
        OnvifDiscoveryEventAccess::upsert(*state.service, std::move(device));
        return;
    }

    addOrMergeDevice(state.devices, std::move(device));
}

void collectProbeMatches(soap* context, wsdd__ProbeMatchesType* matches) {
    WsddEventState* state = stateFrom(context);
    if (state == nullptr || matches == nullptr || matches->ProbeMatch == nullptr) {
        return;
    }

    for (int index = 0; index < matches->__sizeProbeMatch; ++index) {
        captureDevice(*state, deviceFromProbeMatch(matches->ProbeMatch[index]));
    }
}

void collectResolveMatch(soap* context, wsdd__ResolveMatchType* match) {
    WsddEventState* state = stateFrom(context);
    if (state == nullptr || match == nullptr) {
        return;
    }

    OnvifDiscoveredDevice device = deviceFromResolveMatch(*match);
    if (state->kind == WsddStateKind::Resolve) {
        state->resolvedDevice = device;
    }
    captureDevice(*state, std::move(device));
}

} // namespace

OnvifDiscoveryService::OnvifDiscoveryService(OnvifDiscoveryOptions options)
    : options_(options) {}

OnvifDiscoveryService::~OnvifDiscoveryService() {
    stop();
}

void OnvifDiscoveryService::start() {
    if (timer_ != nullptr || passiveRunning_.load()) {
        return;
    }

    passiveRunning_.store(true);
    passiveListener_ = std::thread([this] { runPassiveListener(); });

    // 주기 Probe는 Hello/Bye 누락이나 multicast 손실을 보정하는 refresh 역할을 한다.
    timer_ = new IntervalTimer(options_.interval, [this] {
        std::vector<OnvifDiscoveredDevice> found = discoverOnce();
        for (OnvifDiscoveredDevice& device : found) {
            upsertDevice(std::move(device));
        }
    });
}

void OnvifDiscoveryService::stop() {
    passiveRunning_.store(false);

    if (timer_ != nullptr) {
        timer_->stop();
        delete timer_;
        timer_ = nullptr;
    }

    if (passiveListener_.joinable()) {
        passiveListener_.join();
    }
}

std::vector<OnvifDiscoveredDevice> OnvifDiscoveryService::discoverOnce() {
    WsddEventState state{WsddStateKind::Probe};

    auto context = makeSoapContext();
    context->user = &state;

    // ProbeMatches는 Probe를 보낸 UDP socket으로 돌아오므로 같은 context로 bind/send/listen한다.
    const SOAP_SOCKET socket = soap_bind(context.get(), nullptr, options_.listenPort, 100);
    if (!soap_valid_socket(socket)) {
        LogSync::error("[ONVIF] discovery listener bind failed: ", *soap_faultstring(context.get()));
        return {};
    }

    const char* id = soap_wsa_rand_uuid(context.get());
    state.messageId = copyString(id);

    if (soap_wsdd_Probe(context.get(), SOAP_WSDD_ADHOC, SOAP_WSDD_TO_TS, kDiscoveryEndpoint,
            id, nullptr, kOnvifNetworkVideoTransmitter, nullptr, nullptr) != SOAP_OK) {
        LogSync::error("[ONVIF] discovery probe failed: ", *soap_faultstring(context.get()));
        soap_closesock(context.get());
        return {};
    }

    const int result = soap_wsdd_listen(context.get(), listenTimeoutMicros(options_.listenTimeout));
    soap_closesock(context.get());

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] discovery listen failed: ", *soap_faultstring(context.get()));
        return {};
    }

    LogSync::info("[ONVIF] discovery found ", state.devices.size(), " device(s)");
    return state.devices;
}

std::optional<OnvifDiscoveredDevice> OnvifDiscoveryService::resolveOnce(const std::string& endpointReference) {
    if (endpointReference.empty()) {
        return std::nullopt;
    }

    WsddEventState state{WsddStateKind::Resolve};

    auto context = makeSoapContext();
    context->user = &state;

    // ResolveMatches도 요청을 보낸 UDP socket으로 돌아오므로 같은 context로 bind/send/listen한다.
    const SOAP_SOCKET socket = soap_bind(context.get(), nullptr, options_.listenPort, 100);
    if (!soap_valid_socket(socket)) {
        LogSync::error("[ONVIF] resolve listener bind failed: ", *soap_faultstring(context.get()));
        return std::nullopt;
    }

    const char* id = soap_wsa_rand_uuid(context.get());
    state.messageId = copyString(id);

    if (soap_wsdd_Resolve(context.get(), SOAP_WSDD_ADHOC, SOAP_WSDD_TO_TS, kDiscoveryEndpoint,
            id, nullptr, endpointReference.c_str()) != SOAP_OK) {
        LogSync::error("[ONVIF] resolve request failed: ", *soap_faultstring(context.get()));
        soap_closesock(context.get());
        return std::nullopt;
    }

    const int result = soap_wsdd_listen(context.get(), listenTimeoutMicros(options_.listenTimeout));
    soap_closesock(context.get());

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] resolve listen failed: ", *soap_faultstring(context.get()));
        return std::nullopt;
    }

    if (state.resolvedDevice) {
        upsertDevice(*state.resolvedDevice);
    }

    return state.resolvedDevice;
}

std::vector<OnvifDiscoveredDevice> OnvifDiscoveryService::devices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return devices_;
}

void OnvifDiscoveryService::runPassiveListener() {
    WsddEventState state{WsddStateKind::PassiveListener};
    state.service = this;

    auto context = makeSoapContext();
    context->user = &state;

    // WS-Discovery Hello/Bye는 well-known UDP 3702로 들어오므로 passive 전용 socket을 유지한다.
    const SOAP_SOCKET socket = soap_bind(context.get(), nullptr, options_.passiveListenPort, 100);
    if (!soap_valid_socket(socket)) {
        LogSync::error("[ONVIF] passive listener bind failed: ", *soap_faultstring(context.get()));
        passiveRunning_.store(false);
        return;
    }

    LogSync::info("[ONVIF] passive discovery listener started on UDP port ", options_.passiveListenPort);

    while (passiveRunning_.load()) {
        const int result = soap_wsdd_listen(context.get(), listenTimeoutMicros(options_.passiveListenPollTimeout));
        if (result != SOAP_OK && passiveRunning_.load()) {
            LogSync::error("[ONVIF] passive listener failed: ", *soap_faultstring(context.get()));
        }
    }

    soap_closesock(context.get());
    LogSync::info("[ONVIF] passive discovery listener stopped");
}

void OnvifDiscoveryService::upsertDevice(OnvifDiscoveredDevice device) {
    std::lock_guard<std::mutex> lock(mutex_);
    addOrMergeDevice(devices_, std::move(device));
}

void OnvifDiscoveryService::markDeviceOffline(const std::string& endpointReference) {
    if (endpointReference.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = std::find_if(devices_.begin(), devices_.end(), [&](const OnvifDiscoveredDevice& device) {
        return device.endpointReference == endpointReference;
    });

    if (it != devices_.end()) {
        it->online = false;
    }
}

// gSOAP WS-Discovery plugin이 요구하는 event handlers. 이름과 signature를 바꾸면 안 된다.
void wsdd_event_Hello(soap* context, unsigned int, const char*, unsigned int, const char*, const char*,
    const char* EndpointReference, const char* Types, const char* Scopes, const char*, const char* XAddrs,
    unsigned int MetadataVersion) {
    WsddEventState* state = stateFrom(context);
    if (state == nullptr) {
        return;
    }

    captureDevice(*state, deviceFromFields(EndpointReference, Types, Scopes, XAddrs, MetadataVersion, true));
}

void wsdd_event_Bye(soap* context, unsigned int, const char*, unsigned int, const char*, const char*,
    const char* EndpointReference, const char*, const char*, const char*, const char*, unsigned int*) {
    WsddEventState* state = stateFrom(context);
    if (state == nullptr) {
        return;
    }

    if (state->service != nullptr) {
        OnvifDiscoveryEventAccess::markOffline(*state->service, copyString(EndpointReference));
    }
}

soap_wsdd_mode wsdd_event_Probe(soap*, const char*, const char*, const char*, const char*,
    const char*, wsdd__ProbeMatchesType*) {
    return SOAP_WSDD_ADHOC;
}

void wsdd_event_ProbeMatches(soap* context, unsigned int, const char*, unsigned int,
    const char*, const char*, wsdd__ProbeMatchesType* matches) {
    collectProbeMatches(context, matches);
}

soap_wsdd_mode wsdd_event_Resolve(soap*, const char*, const char*, const char*, wsdd__ResolveMatchType*) {
    return SOAP_WSDD_ADHOC;
}

void wsdd_event_ResolveMatches(soap* context, unsigned int, const char*, unsigned int,
    const char*, const char*, wsdd__ResolveMatchType* match) {
    collectResolveMatch(context, match);
}
