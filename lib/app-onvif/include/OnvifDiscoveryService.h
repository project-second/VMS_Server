#ifndef ONVIF_DISCOVERY_SERVICE_H
#define ONVIF_DISCOVERY_SERVICE_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

class IntervalTimer;
struct OnvifDiscoveryEventAccess;

/// ONVIF WS-Discovery에서 발견한 장비 정보.
/// XAddrs는 이후 Device Service 호출에 사용할 실제 ONVIF endpoint 후보이다.
struct OnvifDiscoveredDevice {
    std::string endpointReference;
    std::string types;
    std::string scopes;
    std::vector<std::string> xaddrs;
    std::uint32_t metadataVersion = 0;
    bool online = true;
};

/// Discovery 동작 옵션.
/// listenPort는 active Probe/Resolve 응답 수신용이며 0이면 OS가 임시 포트를 할당한다.
/// passiveListenPort는 Hello/Bye 같은 passive WS-Discovery event를 듣는 포트이다.
struct OnvifDiscoveryOptions {
    std::chrono::milliseconds interval{300000};
    std::chrono::milliseconds listenTimeout{3000};
    std::chrono::milliseconds passiveListenPollTimeout{500};
    int listenPort = 0;
    int passiveListenPort = 3702;
};

/// ONVIF WS-Discovery 서비스.
///
/// - start(): passive listener와 주기 Probe를 함께 시작한다.
/// - discoverOnce(): multicast Probe를 한 번 보내고 ProbeMatches를 수집한다.
/// - resolveOnce(): EndpointReference에 대해 Resolve를 보내고 ResolveMatches를 수집한다.
///
/// gSOAP WS-Discovery plugin은 전역 callback 함수를 호출하므로, 구현부에서는
/// soap->user에 작업 상태를 넣어 active Probe, Resolve, passive listener를 구분한다.
class OnvifDiscoveryService {
public:
    explicit OnvifDiscoveryService(OnvifDiscoveryOptions options = {});
    ~OnvifDiscoveryService();

    OnvifDiscoveryService(const OnvifDiscoveryService&) = delete;
    OnvifDiscoveryService& operator=(const OnvifDiscoveryService&) = delete;

    /// passive listener thread와 주기 Probe timer를 시작한다.
    void start();

    /// 실행 중인 timer/listener를 중지하고 thread 종료를 기다린다.
    void stop();

    /// multicast Probe를 한 번 수행하고 응답으로 받은 장비 목록을 반환한다.
    std::vector<OnvifDiscoveredDevice> discoverOnce();

    /// EndpointReference에 대한 Resolve를 수행해 XAddrs를 다시 조회한다.
    std::optional<OnvifDiscoveredDevice> resolveOnce(const std::string& endpointReference);

    /// 마지막으로 수집한 장비 상태 snapshot을 반환한다.
    std::vector<OnvifDiscoveredDevice> devices() const;

    void setDiscoveryInterval(std::chrono::milliseconds interval);
    void setListenTimeout(std::chrono::milliseconds timeout);
    void setPassiveListenPollTimeout(std::chrono::milliseconds timeout);
    void setListenPort(int port);
    void setPassiveListenPort(int port);
    void setOptions(OnvifDiscoveryOptions options);

private:
    friend struct OnvifDiscoveryEventAccess;

    void runPassiveListener();
    void upsertDevice(OnvifDiscoveredDevice device);
    void markDeviceOffline(const std::string& endpointReference);

    OnvifDiscoveryOptions options_;
    std::vector<OnvifDiscoveredDevice> devices_;
    mutable std::mutex mutex_;
    IntervalTimer* timer_ = nullptr;
    std::atomic<bool> passiveRunning_{false};
    std::thread passiveListener_;
};

#endif // ONVIF_DISCOVERY_SERVICE_H
