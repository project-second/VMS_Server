/**
 * @file OnvifDeviceProvisioningService.cpp
 * @brief 발견된 ONVIF 장치의 상세 정보 수집과 DB·미디어 등록 조율 구현.
 */
#include "OnvifDeviceProvisioningService.h"

// 외부 스레드풀을 참조하고, 카메라·릴레이 구성 요소와 내부 저장소를 MediaService에 연결한다.
// 전달받은 외부 구성 요소는 이 서비스보다 오래 유지되어야 한다.
OnvifDeviceProvisioningService::OnvifDeviceProvisioningService(
    ThreadPool& tp,
    CameraManager& cameraManager,
    RtspRelayServer& relayServer,
    int discoveryIntervalMs,
    MediaRecordingOptions recordingOptions)
    : threadPool{tp}, mediaService{cameraManager, relayServer, repository, std::move(recordingOptions)} {
    // WS-Discovery 탐색 간격이며, 프로비저닝 주기가 끝난 뒤의 5초 대기와는 별개다.
    discoveryService.setDiscoveryInterval(std::chrono::milliseconds(discoveryIntervalMs));
}

// 멤버 자원이 파괴되기 전에 탐색과 프로비저닝 스레드의 종료를 기다린다.
OnvifDeviceProvisioningService::~OnvifDeviceProvisioningService() {
    stop();
}

// 실행 중이면 중복 시작을 건너뛰고, 탐색 서비스와 결과 동기화용 스레드를 시작한다.
void OnvifDeviceProvisioningService::start() {
    if (running.exchange(true)) {
        return;
    }
    discoveryService.start();

    provisioningThread = std::thread([this]() { provisioningLoop(); });
}

// 다음 반복을 중지하도록 요청하고 탐색을 멈춘 뒤 프로비저닝 스레드의 종료를 기다린다.
void OnvifDeviceProvisioningService::stop() {
    running.store(false);
    discoveryService.stop();

    // 현재 주기의 작업 결과 수집·미디어 반영과 5초 대기가 끝나야 join이 완료된다.
    if (provisioningThread.joinable()) {
        provisioningThread.join();
    }
}

// 장치별 SOAP 조회는 병렬로 수행하고, 결과 수집과 DB·미디어 반영은 이 루프에서 순서대로 수행한다.
void OnvifDeviceProvisioningService::provisioningLoop() {
    while (running.load()) {
        // 탐색 스레드가 갱신하는 목록을 복사해 이번 주기에 사용할 장치 스냅샷을 얻는다.
        std::vector<OnvifDiscoveredDevice> devices = discoveryService.devices();
        std::vector<std::future<std::optional<ProvisioningResult>>> futures;

        for (const auto& device : devices) {
            // 온라인 상태이고 하나 이상의 서비스 주소가 있는 장치만 조회 작업을 제출한다.
            if (!device.online || device.xaddrs.empty()) { continue; }

            // 장치 정보는 값으로 캡처하고, 장치별 작업을 우선순위 0으로 스레드풀에 제출한다.
            futures.push_back(threadPool.submit(0, [device]() -> std::optional<ProvisioningResult> {
                // 발견된 주소 중 첫 번째 주소를 Device Service 호출에 사용한다.
                const std::string xaddr = device.xaddrs.front();

                // 클라이언트는 작업마다 생성해 장치 간에 공유하지 않는다.
                OnvifDeviceClient deviceClient;
                OnvifMediaClient mediaClient;

                // 장치 정보와 기능을 조회한 뒤 Media Service 주소를 얻어 후속 호출에 사용한다.
                auto info = deviceClient.getDeviceInformation(xaddr);
                auto capabilities = deviceClient.getCapabilities(xaddr);

                // 필수 정보나 Media Service 주소가 없으면 이번 주기의 해당 장치 결과를 제외한다.
                if (!info || !capabilities || capabilities->mediaXAddr.empty()) { return std::nullopt; }

                auto profiles = mediaClient.getProfiles(capabilities->mediaXAddr);
                // 조회에 성공한 빈 목록은 허용하고, 조회 자체에 실패한 경우만 장치 결과를 제외한다.
                if (!profiles) { return std::nullopt; }

                // 탐색 정보와 Device/Media 응답을 DB·미디어 반영에 사용할 하나의 결과로 묶는다.
                return ProvisioningResult {
                    .discoveredDevice = device,
                    .deviceServiceUrl = xaddr,
                    .mediaServiceUrl = capabilities->mediaXAddr,
                    .deviceInformation = *info,
                    .capabilities = *capabilities,
                    .profiles = [&]() {
                        std::vector<OnvifProvisionedProfile> out;
                        // URI 조회는 프로파일별로 순차 수행하며, 실패해도 프로파일과 빈 optional을 유지한다.
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

        // 제출 순서대로 완료를 기다리고, 유효한 장치 결과만 모은다.
        for (auto& future : futures) {
            try {
                auto result = future.get();
                if (result) {
                    results.push_back(std::move(*result));
                }
            } catch (const std::exception& e) {
                // 작업 예외는 future.get()에서 전달되며, 로그를 남긴 뒤 다른 장치 결과 수집을 계속한다.
                LogSync::error("[Provisioning] Exception in provisioning task: ", e.what());
            } catch (...) {
                LogSync::error("[Provisioning] Unknown exception in provisioning task");
            }
        }

        // 장치·채널·프로파일의 DB 반영과 사용 가능한 URI의 녹화·RTSP 릴레이 등록을 위임한다.
        mediaService.addCamera(results);

        // 이번 주기의 조회와 반영이 모두 끝난 시점부터 5초 대기한 뒤 다음 스냅샷을 읽는다.
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}
