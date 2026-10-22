/**
 * @file OnvifDeviceProvisioningService.h
 * @brief ONVIF 장치 상세 정보 수집과 DB·미디어 구성 동기화 서비스 인터페이스.
 */
#ifndef __ONVIFDEVICEPROVISIONINGSERVICE_H__
#define __ONVIFDEVICEPROVISIONINGSERVICE_H__

#include "ThreadPool.hpp"
#include "LogSync.h"

#include "OnvifDiscoveryService.h"
#include "OnvifDeviceClient.h"
#include "OnvifMediaClient.h"
#include "OnvifProvisioningRepository.h"
#include "OnvifProvisioningTypes.h"
#include "CameraManager.h"
#include "MediaService.h"
#include "RtspRelayServer.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

/**
 * @brief 발견된 ONVIF 장치 정보를 수집해 VMS DB와 미디어 구성에 반영하는 애플리케이션 서비스.
 *
 * - OnvifDiscoveryService가 유지하는 장치 스냅샷을 읽는다.
 * - 장치별 ONVIF SOAP 조회 작업을 ThreadPool에 제출한다.
 * - DeviceInformation, Capabilities, Profiles, StreamUri를 ProvisioningResult로 모은다.
 * - MediaService에 결과를 전달해 OnvifProvisioningRepository를 통한 DB 반영과 녹화·릴레이 등록을 조율한다.
 *
 * 장치 내부의 호출은 Media Service 주소와 프로파일 토큰의 의존 관계에 따라 순차 수행하고,
 * 여러 장치 간 호출은 스레드풀로 병렬 처리한다.
 */
class OnvifDeviceProvisioningService {
private:
    ThreadPool& threadPool; ///< 장치별 SOAP 조회 작업을 실행하는 외부 스레드풀 참조.
    OnvifDiscoveryService discoveryService; ///< 장치 탐색 결과와 온라인 상태를 관리하는 WS-Discovery 서비스.

    /**
     * @brief 장치별 Device Service 클라이언트 보관용 컨테이너. 현재 프로비저닝 루프에서는 사용하지 않는다.
     * @todo 장치별 인증 옵션과 자격 증명을 관리하게 되면 클라이언트 생성 정책을 정리한다.
     */
    std::vector<std::unique_ptr<OnvifDeviceClient>> deviceClients;
    OnvifProvisioningRepository repository; ///< 장치·채널·프로파일 메타데이터 저장소.
    MediaService mediaService; ///< DB 반영과 녹화·RTSP 릴레이 등록을 조율하는 서비스.

    std::thread provisioningThread; ///< 장치 상세 정보 조회와 결과 반영을 반복하는 작업 스레드.
    std::atomic<bool> running{false}; ///< 프로비저닝 루프의 다음 반복을 계속할지 나타내는 원자 플래그.

public:
    /**
     * @brief 장치 조회에 사용할 외부 구성 요소를 연결하고 탐색·녹화 설정을 적용한다.
     *
     * 전달받은 스레드풀, 카메라 관리자 및 릴레이 서버는 외부에서 소유하며 이 서비스보다 오래 유지되어야 한다.
     * @param tp 장치별 SOAP 조회 작업을 실행할 스레드풀.
     * @param cameraManager 녹화용 카메라를 등록할 관리자.
     * @param relayServer RTSP 릴레이에 카메라를 등록할 서버.
     * @param discoveryIntervalMs WS-Discovery 탐색 간격(ms). 프로비저닝 주기 사이의 5초 대기와는 별개다.
     * @param recordingOptions 녹화 루트 디렉터리와 파일 분할 간격 설정.
     */
    OnvifDeviceProvisioningService(
        ThreadPool& tp,
        CameraManager& cameraManager,
        RtspRelayServer& relayServer,
        int discoveryIntervalMs,
        MediaRecordingOptions recordingOptions = {});
    /**
     * @brief 탐색 서비스와 프로비저닝 스레드를 중지하고 종료를 기다린다.
     */
    ~OnvifDeviceProvisioningService();

    /**
     * @brief WS-Discovery 탐색과 프로비저닝 작업 스레드를 시작한다.
     *
     * 이미 실행 중이면 중복 시작 요청을 건너뛴다.
     */
    void start();

    /**
     * @brief 다음 프로비저닝 반복을 중지하도록 요청하고 탐색과 작업 스레드의 종료를 기다린다.
     *
     * 진행 중인 주기가 있으면 작업 결과 수집, 미디어 반영 및 주기 말미의 5초 대기가 끝날 때까지 기다린다.
     */
    void stop();

    /**
     * @brief 탐색 스냅샷의 온라인 장치를 병렬 조회하고 수집한 결과를 DB·미디어 구성에 반영한다.
     *
     * start()가 생성한 스레드에서 실행한다. 장치별 작업의 결과를 수집한 뒤 MediaService에 전달하고,
     * 각 주기 처리가 끝난 시점부터 5초 대기한다. running이 false이면 다음 반복을 종료한다.
     */
    void provisioningLoop();
};

#endif // __ONVIFDEVICEPROVISIONINGSERVICE_H__
