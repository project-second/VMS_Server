#ifndef __ONVIFDEVICEPROVISIONINGSERVICE_H__
#define __ONVIFDEVICEPROVISIONINGSERVICE_H__

#include "ThreadPool.hpp"
#include "LogSync.h"

#include "OnvifDiscoveryService.h"
#include "OnvifDeviceClient.h"
#include "OnvifMediaClient.h"
#include "OnvifProvisioningRepository.h"
#include "OnvifProvisioningTypes.h"

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

// Discovery로 찾은 ONVIF 장비를 실제 VMS DB 데이터로 동기화하는 application service다.
//
// 책임:
// - OnvifDiscoveryService가 유지하는 장비 snapshot을 읽는다.
// - 장비별 ONVIF SOAP 호출을 ThreadPool에 제출한다.
// - DeviceInformation, Capabilities, Profiles, StreamUri를 ProvisioningResult로 모은다.
// - 모은 결과를 OnvifProvisioningRepository에 넘겨 DB에 반영한다.
//
// 장비 하나 안의 ONVIF 호출은 mediaXAddr/profileToken 의존 관계가 있으므로 순차 처리하고,
// 여러 장비 간 호출만 ThreadPool로 병렬화한다.
class OnvifDeviceProvisioningService {
private:
    ThreadPool& threadPool;
    OnvifDiscoveryService discoveryService;

    // TODO: 장비별 인증 옵션/credential을 관리하게 되면 client 생성 정책을 여기서 정리한다.
    std::vector<std::unique_ptr<OnvifDeviceClient>> deviceClients;
    OnvifProvisioningRepository repository;

    std::thread provisioningThread;
    std::atomic<bool> running{false};

public:
    OnvifDeviceProvisioningService(ThreadPool& tp, int discoveryIntervalMs);
    ~OnvifDeviceProvisioningService();

    // Discovery와 provisioning loop를 시작한다.
    void start();

    // Discovery와 provisioning loop를 중지한다.
    void stop();

    // 주기적으로 discovery snapshot을 읽고 provisioning 작업을 수행하는 worker loop다.
    void provisioningLoop();
};

#endif // __ONVIFDEVICEPROVISIONINGSERVICE_H__
