#include "app/VmsApplication.h"

#include "ApplicationConfigLoader.h"
#include "CameraManager.h"
#include "DbRuntime.hpp"
#include "HttpServer.h"
#include "OnvifDeviceProvisioningService.h"
#include "RtspRelayServer.h"
#include "OnvifSoapContext.h"
#include "ThreadPool.hpp"
#include "VmsAdminController.h"
#include "VmsAdminRepository.h"
#include "VmsAdminService.h"
#include "app/VmsAuthMiddleware.h"
#include "app/VmsCreateAdminCommand.h"
#include "VmsDeviceController.h"
#include "VmsDeviceRepository.h"
#include "VmsDeviceService.h"
#include "VmsPlaybackController.h"
#include "VmsPlaybackRepository.h"
#include "VmsPlaybackService.h"
#include "app/VmsRuntimePaths.h"
#include "VmsUserController.h"
#include "VmsUserRepository.h"
#include "VmsUserService.h"

#include <gst/gst.h>

#include <atomic>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace {

volatile std::sig_atomic_t g_shutdown_requested = 0;

void handle_shutdown_signal(int signal_number) {
    if (signal_number == SIGINT || signal_number == SIGTERM) {
        g_shutdown_requested = 1;
    }
}

void waitForShutdownSignal() {
#if defined(__unix__) || defined(__APPLE__)
    while (g_shutdown_requested == 0) {
        usleep(100 * 1000);
    }
#else
    while (g_shutdown_requested == 0) { }
#endif
}

} // namespace

int VmsApplication::run(int argc, char const* argv[]) {
    const VmsRuntimePaths paths = resolveRuntimePaths(argc > 0 ? argv[0] : nullptr);

    AppRuntimeConfig config;
    if (!loadApplicationConfig(config, paths.resourceDirectory.string(), "application.yml")) {
        return 1;
    }
    normalizeRuntimeConfigPaths(config, paths);

    if (argc > 1) {
        if (std::string(argv[1]) == "create-admin") {
            return runCreateAdminCommand(argc, argv, config);
        }
        printVmsUsage(argv[0]);
        return 2;
    }

    configureOnvifSoapLogging(OnvifSoapLogOptions{
        .enabled = config.onvifSoapLogEnabled,
        .directory = config.onvifSoapLogDir,
    });

    AppDb::configure(config.dbConninfo, config.dbMapperPath);

    configurePackagedGStreamerEnvironment(paths);
    gst_init(nullptr, nullptr);

    VmsPlaybackRepository playbackRepository;
    const std::filesystem::path playbackCacheRoot =
        std::filesystem::path(config.mediaRecordingRootDir).parent_path() / "playback-cache";
    VmsPlaybackService playbackService(playbackRepository, playbackCacheRoot);
    if (!playbackService.interruptStaleRecordings()) {
        std::cerr << "[Playback] failed to mark stale recording segments interrupted" << std::endl;
    }

    CameraManager cameraManager;
    cameraManager.segment_store().set_change_callback(
        [&playbackService](const RecordingSegment& segment) {
            if (!playbackService.persistRecordingSegment(segment)) {
                std::cerr << "[Playback] failed to persist segment: "
                          << segment.file_path << std::endl;
            }
        });
    RtspRelayServer relayServer(
        std::to_string(config.mediaRtspRelayPort),
        config.mediaRtspRelayBindAddress,
        config.mediaRtspRelayAdvertisedHost,
        config.mediaRtspRelayEnabled);

    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);

    HttpServer server(config.serverHost, config.serverPort, config.serverThreads);
    VmsUserRepository userRepository;
    VmsUserService userService(userRepository);
    VmsAdminRepository adminRepository;
    VmsAdminService adminService(adminRepository);
    VmsDeviceRepository deviceRepository;
    VmsDeviceService deviceService(deviceRepository);

    VmsAuthMiddleware authMiddleware(userService, adminService);
    server.setMiddleware([&authMiddleware](Http::Request& req) -> std::optional<Http::Response> {
        return authMiddleware(req);
    });

    VmsUserController userController(server, userService);
    VmsAdminController adminController(server, adminService);
    VmsDeviceController deviceController(server, deviceService);
    VmsPlaybackController playbackController(server, playbackService);

    ThreadPool onvifThreadPool;
    OnvifDeviceProvisioningService onvifProvisioning(
        onvifThreadPool,
        cameraManager,
        relayServer,
        config.onvifDiscoveryIntervalMs,
        MediaRecordingOptions{
            .recordRootDir = config.mediaRecordingRootDir,
            .splitSeconds = config.mediaRecordingSplitSeconds,
        });

    if (!relayServer.start()) {
        return 1;
    }

    if (!server.start()) {
        relayServer.stop();
        return 1;
    }

    cameraManager.start_all();
    onvifProvisioning.start();

    waitForShutdownSignal();

    std::cout << std::endl;
    std::cout << "Shutdown signal received" << std::endl;
    std::cout << "Stopping ONVIF provisioning..." << std::endl;
    onvifProvisioning.stop();
    std::cout << "Stopping CameraManager..." << std::endl;
    cameraManager.stop_all();
    std::cout << "Stopping RtspRelayServer..." << std::endl;
    relayServer.stop();

    return 0;
}
