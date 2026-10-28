/**
 * @file OnvifDiscoveryService.h
 * @brief ONVIF WS-Discovery 능동 탐색과 수동 이벤트 수신 및 장치 상태 관리 인터페이스.
 */
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

/**
 * @brief ONVIF WS-Discovery로 수집한 장치 식별 정보와 서비스 주소 후보.
 *
 * XAddrs는 이후 Device Service 호출에 사용할 ONVIF 엔드포인트 후보이다.
 */
struct OnvifDiscoveredDevice {
    std::string endpointReference; ///< 장치의 WS-Addressing EndpointReference 주소.
    std::string types; ///< 장치가 광고한 Types 문자열.
    std::string scopes; ///< 장치가 광고한 Scopes 문자열.
    std::vector<std::string> xaddrs; ///< 공백으로 구분된 XAddrs를 분리한 서비스 주소 목록.
    std::uint32_t metadataVersion = 0; ///< 수신한 메타데이터 버전. 0은 미지정 값으로 취급한다.
    bool online = true; ///< 수신 이벤트로 관리하는 온라인 상태. Bye 수신 시 false로 표시한다.
};

/**
 * @brief 주기 Probe와 능동 응답·수동 이벤트 수신에 사용할 탐색 옵션.
 */
struct OnvifDiscoveryOptions {
    std::chrono::milliseconds interval{300000}; ///< 주기 Probe 실행 간격. 타이머 생성 시 적용한다.
    std::chrono::milliseconds listenTimeout{3000}; ///< 능동 Probe/Resolve 응답의 수신 대기 시간.
    std::chrono::milliseconds passiveListenPollTimeout{500}; ///< 수동 이벤트 수신 루프의 대기 시간.
    int listenPort = 0; ///< 능동 응답 수신용 UDP 포트. 0이면 OS가 임시 포트를 할당한다.
    int passiveListenPort = 3702; ///< Hello/Bye 등 수동 이벤트를 수신할 UDP 포트.
};

/**
 * @brief ONVIF WS-Discovery 탐색과 이벤트 수신으로 장치 상태를 유지하는 서비스.
 *
 * - start(): 수동 이벤트 수신 스레드와 즉시 1회 실행을 포함한 주기 Probe를 시작한다.
 * - discoverOnce(): 멀티캐스트 Probe를 한 번 보내고 해당 호출의 장치 목록을 반환한다.
 * - resolveOnce(): EndpointReference의 서비스 주소를 조회하고 수신 결과를 공유 목록에 반영한다.
 *
 * gSOAP 전역 콜백은 soap->user의 작업 상태로 Probe, Resolve, 수동 수신을 구분한다.
 * 공유 장치 목록은 뮤텍스로 보호하며, 한 번의 Probe에서 응답이 없는 장치도 기존 상태를 유지한다.
 * 옵션 변경은 별도 동기화를 제공하지 않으므로 서비스와 단발 조회가 실행되기 전에 수행한다.
 */
class OnvifDiscoveryService {
public:
    /**
     * @brief 탐색 옵션을 저장한 서비스 객체를 생성한다.
     * @param options 탐색 주기, 수신 대기 시간 및 UDP 포트 설정.
     */
    explicit OnvifDiscoveryService(OnvifDiscoveryOptions options = {});

    /**
     * @brief 타이머와 수신 스레드를 중지하고 자원을 정리한다.
     */
    ~OnvifDiscoveryService();

    /** @brief 서비스 객체의 복사 생성을 금지한다. */
    OnvifDiscoveryService(const OnvifDiscoveryService&) = delete;
    /** @brief 서비스 객체의 복사 대입을 금지한다. */
    OnvifDiscoveryService& operator=(const OnvifDiscoveryService&) = delete;

    /**
     * @brief 수동 이벤트 수신 스레드와 주기 Probe 타이머를 시작한다.
     *
     * 타이머는 시작 직후 Probe를 한 번 실행하고 이후 설정된 간격으로 반복한다.
     * 타이머가 있거나 수신 루프가 실행 중이면 중복 시작을 건너뛴다.
     */
    void start();

    /**
     * @brief 타이머를 중지·해제하고 수동 이벤트 수신 스레드의 종료를 기다린다.
     *
     * 진행 중인 타이머 작업과 수신 대기의 종료를 기다리며, 수집된 장치 목록은 유지한다.
     */
    void stop();

    /**
     * @brief ONVIF 영상 송신 장치에 대한 멀티캐스트 Probe를 한 번 수행한다.
     *
     * 해당 호출의 로컬 결과를 반환하며, 주기 탐색은 이 결과를 공유 장치 목록에 병합한다.
     * @return 응답에서 수집·병합한 장치 목록. 응답이 없거나 바인딩·송신·수신 실패 시 빈 목록.
     */
    std::vector<OnvifDiscoveredDevice> discoverOnce();

    /**
     * @brief EndpointReference에 대한 Resolve를 수행해 서비스 주소를 다시 조회한다.
     *
     * 정상 수신한 결과는 공유 장치 목록에도 추가·병합한다.
     * @param endpointReference 조회할 장치의 WS-Addressing EndpointReference 주소.
     * @return 수신한 장치 정보. 입력이 비어 있거나 응답이 없거나 통신에 실패하면 std::nullopt.
     */
    std::optional<OnvifDiscoveredDevice> resolveOnce(const std::string& endpointReference);

    /**
     * @brief 공유 장치 목록을 잠금 안에서 복사해 상태 스냅샷으로 반환한다.
     * @return 온라인·오프라인 상태를 포함한 현재 장치 목록의 복사본.
     */
    std::vector<OnvifDiscoveredDevice> devices() const;

    /**
     * @brief 다음 타이머 생성에 사용할 주기 Probe 간격을 설정한다.
     * @param interval Probe 실행 간격. 다음 start()에서 생성하는 타이머에 적용한다.
     */
    void setDiscoveryInterval(std::chrono::milliseconds interval);

    /**
     * @brief 능동 Probe/Resolve 응답의 수신 대기 시간을 설정한다.
     * @param timeout 수신 대기 시간. 0 이하는 gSOAP 호출 시 1ms로 보정한다.
     */
    void setListenTimeout(std::chrono::milliseconds timeout);

    /**
     * @brief 수동 이벤트 수신 루프의 대기 시간을 설정한다.
     * @param timeout 수신 대기 시간. 0 이하는 gSOAP 호출 시 1ms로 보정한다.
     */
    void setPassiveListenPollTimeout(std::chrono::milliseconds timeout);

    /**
     * @brief 능동 Probe/Resolve 응답을 수신할 UDP 포트를 설정한다.
     * @param port 단발 조회의 소켓 바인딩에 사용할 포트. 0이면 OS가 임시 포트를 할당한다.
     */
    void setListenPort(int port);

    /**
     * @brief 수동 이벤트를 수신할 UDP 포트를 설정한다.
     * @param port 다음 수동 수신 소켓 생성 시 바인딩할 포트. 기본값은 3702이다.
     */
    void setPassiveListenPort(int port);

    /**
     * @brief 탐색 옵션 전체를 교체한다.
     * @param options 이후 탐색과 수신에 사용할 옵션. 타이머와 소켓은 각각 생성 시 설정을 읽는다.
     */
    void setOptions(OnvifDiscoveryOptions options);

private:
    friend struct OnvifDiscoveryEventAccess; ///< 전역 gSOAP 콜백의 장치 상태 갱신을 중계하는 접근 도우미.

    /**
     * @brief 전용 UDP 소켓에서 수동 WS-Discovery 이벤트를 반복 수신한다.
     *
     * passiveRunning_이 false이면 수신 루프를 종료하고 소켓을 닫는다.
     */
    void runPassiveListener();

    /**
     * @brief 식별자 또는 서비스 주소가 같은 장치를 공유 목록에 추가·병합한다.
     * @param device 추가하거나 기존 장치의 필드를 갱신할 수신 정보.
     */
    void upsertDevice(OnvifDiscoveredDevice device);

    /**
     * @brief EndpointReference가 일치하는 기존 장치를 오프라인으로 표시한다.
     * @param endpointReference 상태를 변경할 장치 주소. 비어 있거나 미등록이면 건너뛴다.
     */
    void markDeviceOffline(const std::string& endpointReference);

    OnvifDiscoveryOptions options_; ///< 탐색 주기와 소켓 생성·수신에 사용할 옵션.
    std::vector<OnvifDiscoveredDevice> devices_; ///< 이벤트와 주기 탐색으로 누적·갱신한 장치 목록.
    mutable std::mutex mutex_; ///< 공유 장치 목록 조회와 상태 갱신을 보호하는 뮤텍스.
    IntervalTimer* timer_ = nullptr; ///< start()에서 생성하고 stop()에서 해제하는 주기 Probe 타이머.
    std::atomic<bool> passiveRunning_{false}; ///< 수동 이벤트 수신 루프의 실행 지속 여부.
    std::thread passiveListener_; ///< 수동 이벤트 수신 작업 스레드.
};

#endif // ONVIF_DISCOVERY_SERVICE_H
