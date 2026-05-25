#ifndef ONVIF_DISCOVERY_SERVICE_H
#define ONVIF_DISCOVERY_SERVICE_H

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class IntervalTimer;

struct OnvifDiscoveredDevice {
    std::string endpointReference;
    std::string types;
    std::string scopes;
    std::vector<std::string> xaddrs;
    std::uint32_t metadataVersion = 0;
};

struct OnvifDiscoveryOptions {
    std::chrono::milliseconds interval{300000};
    std::chrono::milliseconds listenTimeout{3000};
    int listenPort = 0;
};

class OnvifDiscoveryService {
public:
    explicit OnvifDiscoveryService(OnvifDiscoveryOptions options = {});
    ~OnvifDiscoveryService();

    OnvifDiscoveryService(const OnvifDiscoveryService&) = delete;
    OnvifDiscoveryService& operator=(const OnvifDiscoveryService&) = delete;

    void start();
    void stop();

    std::vector<OnvifDiscoveredDevice> discoverOnce();
    std::vector<OnvifDiscoveredDevice> devices() const;

private:
    void replaceDevices(std::vector<OnvifDiscoveredDevice> devices);

    OnvifDiscoveryOptions options_;
    std::vector<OnvifDiscoveredDevice> devices_;
    mutable std::mutex mutex_;
    IntervalTimer* timer_ = nullptr;
};

#endif // ONVIF_DISCOVERY_SERVICE_H
