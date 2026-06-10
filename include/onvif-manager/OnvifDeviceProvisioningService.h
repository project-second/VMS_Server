#ifndef __ONVIFDEVICEPROVISIONINGSERVICE_H__
#define __ONVIFDEVICEPROVISIONINGSERVICE_H__

#include "ThreadPool.hpp"
#include "LogSync.h"

#include "OnvifDiscoveryService.h"
#include "OnvifDeviceClient.h"
#include "OnvifMediaClient.h"

struct OnvifProvisionedProfile {
    OnvifMediaProfile profile;
    std::optional<OnvifStreamUri> streamUri;
};

struct ProvisioningResult {
    OnvifDiscoveredDevice discoveredDevice;

    std::string deviceServiceUrl;
    std::string mediaServiceUrl;

    OnvifDeviceInformation deviceInformation;
    OnvifDeviceCapabilities capabilities;

    std::vector<OnvifProvisionedProfile> profiles;
};

class OnvifDeviceProvisioningService {
private:
    ThreadPool& threadPool;
    OnvifDiscoveryService discoveryService;

    std::vector<std::unique_ptr<OnvifDeviceClient>> deviceClients;

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