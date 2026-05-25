#include "OnvifDiscoveryService.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <vector>


#include "IntervalTimer.h"
#include "LogSync.h"

#include "wsddapi.h"
#include "wsaapi.h"

#include "wsdd.nsmap"

namespace {

constexpr const char* kDiscoveryEndpoint = "soap.udp://239.255.255.250:3702";
constexpr const char* kOnvifNetworkVideoTransmitter = "\"http://www.onvif.org/ver10/network/wsdl\":NetworkVideoTransmitter";

struct DiscoveryState {
    std::string messageId;
    std::vector<OnvifDiscoveredDevice> devices;
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

void addUniqueDevice(std::vector<OnvifDiscoveredDevice>& devices, OnvifDiscoveredDevice device) {
    const auto it = std::find_if(devices.begin(), devices.end(), [&](const OnvifDiscoveredDevice& current) {
        return sameDeviceAddress(current, device);
    });

    if (it == devices.end()) {
        devices.push_back(std::move(device));
    }
}

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

DiscoveryState* stateFrom(soap* context) {
    return context == nullptr ? nullptr : static_cast<DiscoveryState*>(context->user);
}

void collectProbeMatches(soap* context, wsdd__ProbeMatchesType* matches) {
    DiscoveryState* state = stateFrom(context);
    if (state == nullptr || matches == nullptr || matches->ProbeMatch == nullptr) {
        return;
    }

    for (int index = 0; index < matches->__sizeProbeMatch; ++index) {
        const wsdd__ProbeMatchType& match = matches->ProbeMatch[index];

        OnvifDiscoveredDevice device;
        device.endpointReference = copyString(match.wsa5__EndpointReference.Address);
        device.types = copyString(match.Types);
        device.scopes = match.Scopes == nullptr ? std::string{} : copyString(match.Scopes->__item);
        device.xaddrs = splitWhitespace(match.XAddrs);
        device.metadataVersion = match.MetadataVersion;

        addUniqueDevice(state->devices, std::move(device));
    }
}

} // namespace

OnvifDiscoveryService::OnvifDiscoveryService(OnvifDiscoveryOptions options)
    : options_(options) {}

OnvifDiscoveryService::~OnvifDiscoveryService() {
    stop();
}

void OnvifDiscoveryService::start() {
    if (timer_ != nullptr) {
        return;
    }

    timer_ = new IntervalTimer(options_.interval, [this] {
        std::vector<OnvifDiscoveredDevice> found = discoverOnce();
        replaceDevices(std::move(found));
    });
}

void OnvifDiscoveryService::stop() {
    if (timer_ == nullptr) {
        return;
    }

    timer_->stop();
    delete timer_;
    timer_ = nullptr;
}

std::vector<OnvifDiscoveredDevice> OnvifDiscoveryService::discoverOnce() {
    DiscoveryState state;

    auto context = makeSoapContext();
    context->user = &state;

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

std::vector<OnvifDiscoveredDevice> OnvifDiscoveryService::devices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return devices_;
}

void OnvifDiscoveryService::replaceDevices(std::vector<OnvifDiscoveredDevice> devices) {
    std::lock_guard<std::mutex> lock(mutex_);
    devices_ = std::move(devices);
}

void wsdd_event_Hello(soap*, unsigned int, const char*, unsigned int, const char*, const char*,
    const char*, const char*, const char*, const char*, const char*, unsigned int) {}

void wsdd_event_Bye(soap*, unsigned int, const char*, unsigned int, const char*, const char*,
    const char*, const char*, const char*, const char*, const char*, unsigned int*) {}

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

void wsdd_event_ResolveMatches(soap*, unsigned int, const char*, unsigned int,
    const char*, const char*, wsdd__ResolveMatchType*) {}
