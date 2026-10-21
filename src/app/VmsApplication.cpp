#include "app/VmsApplication.h"

#include "ApplicationConfigLoader.h"
#include "CameraManager.h"
#include "DbRuntime.hpp"
#include "H264RelayHub.h"
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

// 시그널 핸들러는 종료 요청만 기록하고, 실제 자원 정리는 run()에서 수행한다.
volatile std::sig_atomic_t g_shutdown_requested = 0;

void handle_shutdown_signal(int signal_number) {
    if (signal_number == SIGINT || signal_number == SIGTERM) {
        g_shutdown_requested = 1;
    }
}

void waitForShutdownSignal() {
#if defined(__unix__) || defined(__APPLE__)
    // 종료 요청을 100ms 간격으로 확인해 대기 중 CPU 사용을 줄인다.
    while (g_shutdown_requested == 0) {
        usleep(100 * 1000);
    }
#else
    while (g_shutdown_requested == 0) { }
#endif
}

} // namespace

int VmsApplication::run(int argc, char const* argv[]) {
    // 실행 파일 위치로 리소스 디렉터리를 정하고 application.yml을 읽는다.
    const VmsRuntimePaths paths = resolveRuntimePaths(argc > 0 ? argv[0] : nullptr);

    AppRuntimeConfig config;
    if (!loadApplicationConfig(config, paths.resourceDirectory.string(), "application.yml")) {
        return 1;
    }

    // 상대 경로로 지정된 DB 매퍼 경로를 실행 파일 디렉터리 기준으로 보정한다.
    normalizeRuntimeConfigPaths(config, paths);

    // 관리자 생성 명령은 서버 구성 요소를 만들기 전에 처리하고 즉시 종료한다.
    if (argc > 1) {
        if (std::string(argv[1]) == "create-admin") {
            return runCreateAdminCommand(argc, argv, config);
        }
        printVmsUsage(argv[0]);
        return 2;
    }

    // ONVIF 장치 통신에서 사용할 SOAP 로그 설정을 적용한다.
    configureOnvifSoapLogging(OnvifSoapLogOptions{
        .enabled = config.onvifSoapLogEnabled,
        .directory = config.onvifSoapLogDir,
    });

    // 각 Repository가 사용할 공통 DB 연결 정보와 SQL 매퍼 경로를 등록한다.
    AppDb::configure(config.dbConninfo, config.dbMapperPath);

    // GStreamer 초기화 전에 배포 패키지의 라이브러리와 플러그인 검색 경로를 설정한다.
    configurePackagedGStreamerEnvironment(paths);
    gst_init(nullptr, nullptr);

    VmsPlaybackRepository playbackRepository;

    // 재생용 캐시는 녹화 루트 디렉터리와 같은 상위 경로의 playback-cache에 둔다.
    const std::filesystem::path playbackCacheRoot =
        std::filesystem::path(config.mediaRecordingRootDir).parent_path() / "playback-cache";
    VmsPlaybackService playbackService(playbackRepository, playbackCacheRoot);

    // 이전 실행에서 완료되지 않은 녹화 정보를 중단 상태로 정리한다. 실패해도 기동은 계속한다.
    if (!playbackService.interruptStaleRecordings()) {
        std::cerr << "[Playback] failed to mark stale recording segments interrupted" << std::endl;
    }

    H264RelayHub relayHub;

    // RTSP 릴레이를 사용할 때만 카메라의 H.264 데이터를 공유 허브로 전달한다.
    CameraManager cameraManager(
        config.mediaRtspRelayEnabled ? &relayHub : nullptr
    );

    // 녹화 세그먼트가 변경될 때 메타데이터를 DB에 반영해 재생 API에서 조회할 수 있게 한다.
    // playbackService는 이 콜백을 사용하는 cameraManager보다 오래 유지되어야 한다.
    cameraManager.segment_store().set_change_callback(
        [&playbackService](const RecordingSegment& segment) {
            if (!playbackService.persistRecordingSegment(segment)) {
                std::cerr << "[Playback] failed to persist segment: "
                          << segment.file_path << std::endl;
            }
        });

    RtspRelayServer relayServer(
        relayHub,
        std::to_string(config.mediaRtspRelayPort),
        config.mediaRtspRelayBindAddress,
        config.mediaRtspRelayAdvertisedHost,
        config.mediaRtspRelayEnabled);

    // Ctrl+C 및 프로세스 종료 요청을 공통 종료 흐름으로 연결한다.
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);

    HttpServer server(config.serverHost, config.serverPort, config.serverThreads);

    // HTTP API의 데이터 접근 계층과 서비스 계층을 구성한다.
    VmsUserRepository userRepository;
    VmsUserService userService(userRepository);
    VmsAdminRepository adminRepository;
    VmsAdminService adminService(adminRepository);
    VmsDeviceRepository deviceRepository;
    VmsDeviceService deviceService(deviceRepository);

    // 요청 처리에 사용자/관리자 인증 미들웨어를 연결한다.
    VmsAuthMiddleware authMiddleware(userService, adminService);
    server.setMiddleware([&authMiddleware](Http::Request& req) -> std::optional<Http::Response> {
        return authMiddleware(req);
    });

    // 각 컨트롤러를 생성하면서 HTTP 라우트와 해당 서비스를 연결한다.
    VmsUserController userController(server, userService);
    VmsAdminController adminController(server, adminService);
    VmsDeviceController deviceController(server, deviceService);
    VmsPlaybackController playbackController(server, playbackService);

    ThreadPool onvifThreadPool;

    // ONVIF 장치 탐색 및 등록에 카메라 관리, 릴레이, 녹화 설정을 함께 전달한다.
    OnvifDeviceProvisioningService onvifProvisioning(
        onvifThreadPool,
        cameraManager,
        relayServer,
        config.onvifDiscoveryIntervalMs,
        MediaRecordingOptions{
            .recordRootDir = config.mediaRecordingRootDir,
            .splitSeconds = config.mediaRecordingSplitSeconds,
        });

    // 릴레이와 HTTP 서버를 먼저 준비한 뒤 카메라 처리와 장치 탐색을 시작한다.
    if (!relayServer.start()) {
        return 1;
    }

    // HTTP 서버 기동에 실패하면 먼저 시작한 릴레이도 정지한다.
    if (!server.start()) {
        relayServer.stop();
        return 1;
    }

    cameraManager.start_all();
    onvifProvisioning.start();

    // 서버와 미디어 작업은 각 실행 흐름에서 동작하고, 여기서는 종료 요청을 기다린다.
    waitForShutdownSignal();

    // 장치 탐색을 멈춘 뒤 카메라 처리를 끝내고, 마지막으로 릴레이를 정지한다.
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
