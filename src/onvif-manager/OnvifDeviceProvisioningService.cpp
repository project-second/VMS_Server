#include "OnvifDeviceProvisioningService.h"

OnvifDeviceProvisioningService::OnvifDeviceProvisioningService(ThreadPool& tp, int discoveryIntervalMs) 
    : threadPool{tp} {
    discoveryService.setDiscoveryInterval(std::chrono::milliseconds(discoveryIntervalMs));
}

OnvifDeviceProvisioningService::~OnvifDeviceProvisioningService() {
    stop();
}

void OnvifDeviceProvisioningService::start() {
    if (running.exchange(true)) {
        return;
    }
    discoveryService.start();

    provisioningThread = std::thread([this]() { provisioningLoop(); });
}

void OnvifDeviceProvisioningService::stop() {
    running.store(false);
    discoveryService.stop();

    if (provisioningThread.joinable()) {
        provisioningThread.join();
    }
}

void OnvifDeviceProvisioningService::provisioningLoop() {
    while (running.load()) {
        std::vector<OnvifDiscoveredDevice> devices = discoveryService.devices();
        std::vector<std::future<std::optional<ProvisioningResult>>> futures;

        for (const auto& device : devices) {
            if (!device.online || device.xaddrs.empty()) { continue; }

            futures.push_back(threadPool.submit(0, [device]() -> std::optional<ProvisioningResult> {
                const std::string xaddr = device.xaddrs.front();

                OnvifDeviceClient deviceClient;
                OnvifMediaClient mediaClient;

                auto info = deviceClient.getDeviceInformation(xaddr);
                auto capabilities = deviceClient.getCapabilities(xaddr);

                if (!info || !capabilities || capabilities->mediaXAddr.empty()) { return std::nullopt; }

                auto profiles = mediaClient.getProfiles(capabilities->mediaXAddr);
                if (!profiles) { return std::nullopt; }

                return ProvisioningResult {
                    .discoveredDevice = device,
                    .deviceServiceUrl = xaddr,
                    .mediaServiceUrl = capabilities->mediaXAddr,
                    .deviceInformation = *info,
                    .capabilities = *capabilities,
                    .profiles = [&]() {
                        std::vector<OnvifProvisionedProfile> out;
                        for (const auto& profile : *profiles) {
                            auto streamUri = mediaClient.getStreamUri(capabilities->mediaXAddr, profile.token);
                            out.push_back(OnvifProvisionedProfile{.profile = profile, .streamUri = streamUri});
                        }
                        return out;
                    }()
                };
            }));
        }

        std::vector<ProvisioningResult> results;

        for (auto& future : futures) {
            try {
                auto result = future.get();
                if (result) {
                    results.push_back(std::move(*result));
                }
            } catch (const std::exception& e) {
                LogSync::error("[Provisioning] Exception in provisioning task: ", e.what());
            } catch (...) {
                LogSync::error("[Provisioning] Unknown exception in provisioning task");
            }
        }

        if (!results.empty() && !repository.upsert(results)) {
            LogSync::error("[Provisioning] failed to upsert provisioning results");
        }

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}
