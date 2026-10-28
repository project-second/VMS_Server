/**
 * @file OnvifDiscoveryService.cpp
 * @brief WS-Discovery 요청·응답과 수동 이벤트를 처리하고 장치 상태를 병합하는 구현.
 */
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
#include "OnvifSoapContext.h"

#include "wsddapi.h"
#include "wsaapi.h"

// 다른 ONVIF SOAP 클라이언트도 공유하는 생성된 네임스페이스 테이블을 이 번역 단위에서 정의한다.
#include "wsdd.nsmap"

/**
 * @brief 전역 gSOAP 이벤트 콜백에서 서비스의 비공개 장치 갱신 기능으로 접근하는 도우미.
 */
struct OnvifDiscoveryEventAccess {
    /**
     * @brief 수신한 장치 정보를 서비스의 공유 목록에 추가·병합한다.
     * @param service 장치 상태를 반영할 탐색 서비스.
     * @param device 추가하거나 병합할 장치 정보.
     */
    static void upsert(OnvifDiscoveryService& service, OnvifDiscoveredDevice device) {
        service.upsertDevice(std::move(device));
    }

    /**
     * @brief EndpointReference로 기존 장치를 찾아 오프라인으로 표시한다.
     * @param service 장치 상태를 반영할 탐색 서비스.
     * @param endpointReference 오프라인으로 표시할 장치 주소.
     */
    static void markOffline(OnvifDiscoveryService& service, const std::string& endpointReference) {
        service.markDeviceOffline(endpointReference);
    }
};

namespace {

constexpr const char* kDiscoveryEndpoint = "soap.udp://239.255.255.250:3702"; ///< WS-Discovery 멀티캐스트 요청 주소.
constexpr const char* kOnvifNetworkVideoTransmitter = "\"http://www.onvif.org/ver10/network/wsdl\":NetworkVideoTransmitter"; ///< Probe의 장치 타입 필터.

/**
 * @brief 같은 gSOAP 콜백을 공유하는 능동 조회와 수동 수신의 작업 구분.
 */
enum class WsddStateKind {
    Probe, ///< 단발 Probe 응답 수집.
    Resolve, ///< 단발 Resolve 응답 수집.
    PassiveListener, ///< 공유 장치 목록에 바로 반영하는 수동 이벤트 수신.
};

/**
 * @brief soap->user를 통해 gSOAP 콜백에 전달하는 작업 상태와 수집 결과.
 */
struct WsddEventState {
    WsddStateKind kind; ///< 컨텍스트가 수행하는 탐색 또는 수신 작업 종류.
    OnvifDiscoveryService* service = nullptr; ///< 수동 이벤트를 반영할 서비스의 비소유 포인터.
    std::string messageId; ///< 능동 요청에 부여한 WS-Addressing 메시지 ID.
    std::vector<OnvifDiscoveredDevice> devices; ///< 해당 호출에서 수집·병합한 장치 목록.
    std::optional<OnvifDiscoveredDevice> resolvedDevice; ///< Resolve 작업에서 마지막으로 수집한 단건 결과.
};

/**
 * @brief 공백으로 구분된 문자열을 개별 항목으로 분리한다.
 * @param value 분리할 문자열. nullptr이면 빈 목록을 반환한다.
 * @return 비어 있지 않은 항목들을 원래 순서대로 담은 목록.
 */
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

/**
 * @brief 선택적인 C 문자열을 소유하는 std::string으로 복사한다.
 * @param value 복사할 문자열. nullptr이면 빈 문자열로 처리한다.
 * @return 입력 문자열의 복사본 또는 빈 문자열.
 */
std::string copyString(const char* value) {
    return value == nullptr ? std::string{} : std::string(value);
}

/**
 * @brief 두 수신 정보가 같은 장치를 나타내는지 식별자와 주소로 판단한다.
 *
 * 비어 있지 않은 EndpointReference가 같으면 일치한다. 그 밖의 경우에도
 * 비어 있지 않은 XAddr를 하나라도 공유하면 같은 장치로 취급한다.
 * @param left 비교할 첫 번째 장치 정보.
 * @param right 비교할 두 번째 장치 정보.
 * @return 식별자 또는 서비스 주소가 일치하면 true, 그 밖에는 false.
 */
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

/**
 * @brief 부분적으로 채워진 수신 정보에서 유효한 필드를 기존 장치에 병합한다.
 *
 * 문자열과 주소 목록은 비어 있지 않을 때 교체하고, 메타데이터 버전은 0이 아닐 때 교체한다.
 * online 상태는 수신 정보의 값을 항상 반영한다.
 * @param[in,out] current 갱신할 기존 장치 정보.
 * @param incoming 새로 수신한 장치 정보.
 */
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

/**
 * @brief 같은 장치가 있으면 첫 번째 일치 항목에 병합하고, 없으면 목록 끝에 추가한다.
 * @param[in,out] devices 장치 정보를 반영할 목록.
 * @param device 추가하거나 병합할 수신 정보.
 */
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

/**
 * @brief 수신 대기 시간을 soap_wsdd_listen()에 전달할 음수 마이크로초 값으로 변환한다.
 * @param timeout 밀리초 단위 수신 대기 시간.
 * @return 음수 마이크로초 값. 0 이하는 -1000으로 보정하고 최대 대기는 2147483647마이크로초로 제한한다.
 */
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

/**
 * @brief WS-Discovery용 공통 네임스페이스와 UDP 송수신 옵션을 설정한다.
 *
 * IPv4 멀티캐스트 TTL은 1, 연결·송수신 타임아웃은 각각 3초로 설정한다.
 * @param context 설정을 적용할 유효한 gSOAP 컨텍스트.
 */
void configureSoapContext(soap* context) {
    soap_set_namespaces(context, namespaces);
    context->connect_flags = SO_BROADCAST;
    context->ipv4_multicast_ttl = 1;
    context->recv_timeout = 3;
    context->send_timeout = 3;
    context->connect_timeout = 3;
}

/**
 * @brief UDP 탐색 컨텍스트를 만들고 공통 설정과 discovery 패킷 로거를 연결한다.
 * @return 소멸 시 로그 경로 등록과 gSOAP 객체·메모리·컨텍스트를 정리하는 소유 포인터.
 */
std::unique_ptr<soap, void (*)(soap*)> makeSoapContext() {
    soap* context = soap_new1(SOAP_IO_UDP | SOAP_XML_IGNORENS);

    configureSoapContext(context);
    applyOnvifSoapLogging(context, "discovery");
    return {context, [](soap* item) {
        if (item == nullptr) {
            return;
        }
        // 컨텍스트 주소로 등록된 로그 정보를 먼저 지운 뒤 gSOAP 자원을 순서대로 정리한다.
        removeOnvifSoapLogging(item);
        soap_destroy(item);
        soap_end(item);
        soap_free(item);
    }};
}

/**
 * @brief 컨텍스트의 user 필드에서 탐색 작업 상태를 조회한다.
 * @pre context가 nullptr이 아니면 user는 nullptr이거나 WsddEventState를 가리켜야 한다.
 * @param context 상태를 조회할 gSOAP 컨텍스트. nullptr도 허용한다.
 * @return 작업 상태의 비소유 포인터. 컨텍스트 또는 user가 nullptr이면 nullptr.
 */
WsddEventState* stateFrom(soap* context) {
    return context == nullptr ? nullptr : static_cast<WsddEventState*>(context->user);
}

/**
 * @brief WS-Discovery 콜백 필드를 복사해 독립적인 장치 정보로 구성한다.
 * @param endpointReference 장치 주소. nullptr이면 빈 문자열로 처리한다.
 * @param types 광고된 장치 타입 문자열. nullptr이면 빈 문자열로 처리한다.
 * @param scopes 광고된 범위 문자열. nullptr이면 빈 문자열로 처리한다.
 * @param xaddrs 공백으로 구분된 서비스 주소. nullptr이면 빈 목록으로 처리한다.
 * @param metadataVersion 수신한 메타데이터 버전.
 * @param online 결과에 부여할 온라인 상태.
 * @return 문자열과 서비스 주소를 소유하는 장치 정보.
 */
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

/**
 * @brief ProbeMatch의 식별 정보와 서비스 주소를 온라인 장치 정보로 변환한다.
 * @param match gSOAP가 역직렬화한 ProbeMatch 항목.
 * @return 응답 필드를 복사하고 online을 true로 설정한 장치 정보.
 */
OnvifDiscoveredDevice deviceFromProbeMatch(const wsdd__ProbeMatchType& match) {
    return deviceFromFields(
        match.wsa5__EndpointReference.Address,
        match.Types,
        match.Scopes == nullptr ? nullptr : match.Scopes->__item,
        match.XAddrs,
        match.MetadataVersion,
        true);
}

/**
 * @brief ResolveMatch의 식별 정보와 서비스 주소를 온라인 장치 정보로 변환한다.
 * @param match gSOAP가 역직렬화한 ResolveMatch 항목.
 * @return 응답 필드를 복사하고 online을 true로 설정한 장치 정보.
 */
OnvifDiscoveredDevice deviceFromResolveMatch(const wsdd__ResolveMatchType& match) {
    return deviceFromFields(
        match.wsa5__EndpointReference.Address,
        match.Types,
        match.Scopes == nullptr ? nullptr : match.Scopes->__item,
        match.XAddrs,
        match.MetadataVersion,
        true);
}

/**
 * @brief 수신 경로에 따라 장치 정보를 공유 목록 또는 해당 호출의 로컬 결과에 반영한다.
 *
 * PassiveListener이고 서비스 포인터가 있으면 공유 목록을 갱신하며,
 * 그 밖의 경우에는 작업 상태의 devices 목록에 추가·병합한다.
 * @param[in,out] state 수신 경로와 결과를 보관하는 작업 상태.
 * @param device 수집한 장치 정보.
 */
void captureDevice(WsddEventState& state, OnvifDiscoveredDevice device) {
    if (state.service != nullptr && state.kind == WsddStateKind::PassiveListener) {
        OnvifDiscoveryEventAccess::upsert(*state.service, std::move(device));
        return;
    }

    addOrMergeDevice(state.devices, std::move(device));
}

/**
 * @brief ProbeMatches의 각 항목을 변환해 컨텍스트의 수신 경로에 따라 수집한다.
 * @param context 작업 상태가 연결된 gSOAP 컨텍스트. 상태가 없으면 건너뛴다.
 * @param matches Probe 응답 목록. 응답이나 항목 배열이 nullptr이면 건너뛴다.
 */
void collectProbeMatches(soap* context, wsdd__ProbeMatchesType* matches) {
    WsddEventState* state = stateFrom(context);
    if (state == nullptr || matches == nullptr || matches->ProbeMatch == nullptr) {
        return;
    }

    for (int index = 0; index < matches->__sizeProbeMatch; ++index) {
        captureDevice(*state, deviceFromProbeMatch(matches->ProbeMatch[index]));
    }
}

/**
 * @brief ResolveMatch를 변환하고 작업 종류에 맞게 단건 결과와 장치 목록에 반영한다.
 *
 * Resolve 작업이면 resolvedDevice를 갱신하고, 장치 목록 반영은 captureDevice()에 위임한다.
 * @param context 작업 상태가 연결된 gSOAP 컨텍스트. 상태가 없으면 건너뛴다.
 * @param match 수신한 ResolveMatch. nullptr이면 건너뛴다.
 */
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

// 옵션을 저장하고, 실제 소켓과 작업 스레드는 탐색 또는 start() 호출 시 생성한다.
OnvifDiscoveryService::OnvifDiscoveryService(OnvifDiscoveryOptions options)
    : options_(options) {}

// 콜백이 참조하는 서비스 상태가 파괴되기 전에 타이머와 수신 스레드를 정리한다.
OnvifDiscoveryService::~OnvifDiscoveryService() {
    stop();
}

// 전용 수신 스레드와 즉시 1회를 포함한 주기 Probe를 함께 시작한다.
void OnvifDiscoveryService::start() {
    if (timer_ != nullptr || passiveRunning_.load()) {
        return;
    }

    passiveRunning_.store(true);
    passiveListener_ = std::thread([this] { runPassiveListener(); });

    // 주기 Probe로 장치 정보와 주소를 다시 수집해 공유 목록을 보강한다.
    // 응답이 없는 기존 장치는 유지하며, 이번에 발견한 장치만 온라인 정보로 추가·병합한다.
    timer_ = new IntervalTimer(options_.interval, [this] {
        std::vector<OnvifDiscoveredDevice> found = discoverOnce();
        for (OnvifDiscoveredDevice& device : found) {
            upsertDevice(std::move(device));
        }
    });
}

// 수신 루프에 중지를 요청하고, 타이머의 진행 중 작업과 수신 스레드가 끝날 때까지 기다린다.
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

// 호출별 상태와 UDP 컨텍스트를 사용해 Probe 응답을 로컬 목록에 모은다.
std::vector<OnvifDiscoveredDevice> OnvifDiscoveryService::discoverOnce() {
    WsddEventState state{WsddStateKind::Probe};

    auto context = makeSoapContext();
    // 전역 이벤트 콜백이 이번 호출의 로컬 수집 상태에 접근하도록 연결한다.
    context->user = &state;

    // ProbeMatches는 Probe를 보낸 UDP socket으로 돌아오므로 같은 context로 bind/send/listen한다.
    const SOAP_SOCKET socket = soap_bind(context.get(), nullptr, options_.listenPort, 100);
    if (!soap_valid_socket(socket)) {
        LogSync::error("[ONVIF] discovery listener bind failed: ", *soap_faultstring(context.get()));
        return {};
    }

    // 요청 메시지 ID와 ONVIF 영상 송신 장치 타입 필터를 지정해 멀티캐스트 Probe를 보낸다.
    const char* id = soap_wsa_rand_uuid(context.get());
    state.messageId = copyString(id);

    if (soap_wsdd_Probe(context.get(), SOAP_WSDD_ADHOC, SOAP_WSDD_TO_TS, kDiscoveryEndpoint,
            id, nullptr, kOnvifNetworkVideoTransmitter, nullptr, nullptr) != SOAP_OK) {
        LogSync::error("[ONVIF] discovery probe failed: ", *soap_faultstring(context.get()));
        soap_closesock(context.get());
        return {};
    }

    // 같은 컨텍스트에서 응답을 처리하며 콜백이 장치를 수집하도록 한 뒤 소켓을 닫는다.
    const int result = soap_wsdd_listen(context.get(), listenTimeoutMicros(options_.listenTimeout));
    soap_closesock(context.get());

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] discovery listen failed: ", *soap_faultstring(context.get()));
        return {};
    }

    LogSync::info("[ONVIF] discovery found ", state.devices.size(), " device(s)");
    return state.devices;
}

// EndpointReference를 대상으로 Resolve를 보내고 수신한 단건 결과를 공유 목록에도 반영한다.
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

    // 능동 Resolve 결과의 공유 반영은 수신 완료 후 이 메서드에서 수행한다.
    if (state.resolvedDevice) {
        upsertDevice(*state.resolvedDevice);
    }

    return state.resolvedDevice;
}

// 수신 콜백과 주기 탐색이 갱신하는 목록을 잠금 안에서 복사해 반환한다.
std::vector<OnvifDiscoveredDevice> OnvifDiscoveryService::devices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return devices_;
}

// 수동 수신 상태에 서비스 주소를 연결해 이벤트가 공유 장치 목록을 바로 갱신하도록 한다.
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

    // 수신 대기를 반복하며 종료 플래그를 확인하고, 실행 중의 수신 오류는 로그로 남긴다.
    while (passiveRunning_.load()) {
        const int result = soap_wsdd_listen(context.get(), listenTimeoutMicros(options_.passiveListenPollTimeout));
        if (result != SOAP_OK && passiveRunning_.load()) {
            LogSync::error("[ONVIF] passive listener failed: ", *soap_faultstring(context.get()));
        }
    }

    soap_closesock(context.get());
    LogSync::info("[ONVIF] passive discovery listener stopped");
}

// 공유 목록을 잠근 상태에서 공통 식별·병합 규칙을 적용한다.
void OnvifDiscoveryService::upsertDevice(OnvifDiscoveredDevice device) {
    std::lock_guard<std::mutex> lock(mutex_);
    addOrMergeDevice(devices_, std::move(device));
}

// Bye에 담긴 EndpointReference와 일치하는 기존 장치의 온라인 상태만 변경한다.
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

// 다음에 생성할 Probe 타이머의 실행 간격을 저장한다.
void OnvifDiscoveryService::setDiscoveryInterval(std::chrono::milliseconds interval) {
    options_.interval = interval;
}

// 능동 Probe/Resolve가 수신 대기에 사용할 시간을 저장한다.
void OnvifDiscoveryService::setListenTimeout(std::chrono::milliseconds timeout) {
    options_.listenTimeout = timeout;
}

// 수동 수신 루프가 gSOAP 수신 함수에 전달할 대기 시간을 저장한다.
void OnvifDiscoveryService::setPassiveListenPollTimeout(std::chrono::milliseconds timeout) {
    options_.passiveListenPollTimeout = timeout;
}

// 이후 생성하는 능동 조회용 UDP 소켓의 바인딩 포트를 저장한다.
void OnvifDiscoveryService::setListenPort(int port) {
    options_.listenPort = port;
}

// 다음 수동 수신 소켓 생성에 사용할 바인딩 포트를 저장한다.
void OnvifDiscoveryService::setPassiveListenPort(int port) {
    options_.passiveListenPort = port;
}

// 옵션 값을 교체하며 타이머와 소켓은 각각 새로 생성할 때 해당 설정을 읽는다.
void OnvifDiscoveryService::setOptions(OnvifDiscoveryOptions options) {
    options_ = options;
}

// gSOAP WS-Discovery plugin이 요구하는 event handlers. 이름과 signature를 바꾸면 안 된다.
/**
 * @brief Hello 이벤트의 장치 정보를 온라인 상태로 수집한다.
 * @param context 작업 상태가 연결된 gSOAP 컨텍스트.
 * @param EndpointReference 수신한 장치 주소.
 * @param Types 장치 타입 문자열.
 * @param Scopes 장치 범위 문자열.
 * @param XAddrs 공백으로 구분된 서비스 주소 문자열.
 * @param MetadataVersion 수신한 메타데이터 버전.
 */
void wsdd_event_Hello(soap* context, unsigned int, const char*, unsigned int, const char*, const char*,
    const char* EndpointReference, const char* Types, const char* Scopes, const char*, const char* XAddrs,
    unsigned int MetadataVersion) {
    WsddEventState* state = stateFrom(context);
    if (state == nullptr) {
        return;
    }

    captureDevice(*state, deviceFromFields(EndpointReference, Types, Scopes, XAddrs, MetadataVersion, true));
}

/**
 * @brief Bye 이벤트의 EndpointReference로 공유 목록의 기존 장치를 오프라인으로 표시한다.
 * @param context 서비스 포인터가 연결된 gSOAP 작업 상태를 제공하는 컨텍스트.
 * @param EndpointReference 오프라인으로 표시할 장치 주소.
 */
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

/**
 * @brief 수신 Probe 요청에 대해 ADHOC 처리 모드를 반환하는 gSOAP 콜백.
 *
 * 이 서비스는 수신 요청에 대한 ProbeMatches 목록을 구성하지 않는다.
 * @return SOAP_WSDD_ADHOC.
 */
soap_wsdd_mode wsdd_event_Probe(soap*, const char*, const char*, const char*, const char*,
    const char*, wsdd__ProbeMatchesType*) {
    return SOAP_WSDD_ADHOC;
}

/**
 * @brief ProbeMatches 이벤트를 공통 응답 수집 함수에 전달한다.
 * @param context 수신 경로와 결과 저장 위치를 제공하는 gSOAP 컨텍스트.
 * @param matches 수신한 ProbeMatch 항목 목록.
 */
void wsdd_event_ProbeMatches(soap* context, unsigned int, const char*, unsigned int,
    const char*, const char*, wsdd__ProbeMatchesType* matches) {
    collectProbeMatches(context, matches);
}

/**
 * @brief 수신 Resolve 요청에 대해 ADHOC 처리 모드를 반환하는 gSOAP 콜백.
 *
 * 이 서비스는 수신 요청에 대한 ResolveMatch를 구성하지 않는다.
 * @return SOAP_WSDD_ADHOC.
 */
soap_wsdd_mode wsdd_event_Resolve(soap*, const char*, const char*, const char*, wsdd__ResolveMatchType*) {
    return SOAP_WSDD_ADHOC;
}

/**
 * @brief ResolveMatches 이벤트를 공통 단건 응답 수집 함수에 전달한다.
 * @param context 수신 경로와 결과 저장 위치를 제공하는 gSOAP 컨텍스트.
 * @param match 수신한 ResolveMatch 항목.
 */
void wsdd_event_ResolveMatches(soap* context, unsigned int, const char*, unsigned int,
    const char*, const char*, wsdd__ResolveMatchType* match) {
    collectResolveMatch(context, match);
}
