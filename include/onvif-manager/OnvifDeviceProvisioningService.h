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

class OnvifDeviceProvisioningService {
private:
    ThreadPool& threadPool;
    OnvifDiscoveryService discoveryService;

    std::vector<std::unique_ptr<OnvifDeviceClient>> deviceClients;
    OnvifProvisioningRepository repository;

    std::thread provisioningThread;
    std::atomic<bool> running{false};

public:
    OnvifDeviceProvisioningService(ThreadPool& tp, int discoveryIntervalMs);
    ~OnvifDeviceProvisioningService();

    void start();
    void stop();

    void provisioningLoop();
};

#endif // __ONVIFDEVICEPROVISIONINGSERVICE_H__