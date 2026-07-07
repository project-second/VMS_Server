#include "ApplicationConfigLoader.h"

#include "DbRuntime.hpp"
#include "HttpServer.h"
#include "OnvifDeviceProvisioningService.h"
#include "ThreadPool.hpp"
#include "VmsAdminBootstrap.h"
#include "VmsAdminController.h"
#include "VmsAdminRepository.h"
#include "VmsAdminService.h"
#include "VmsDeviceController.h"
#include "VmsDeviceRepository.h"
#include "VmsDeviceService.h"
#include "VmsUserController.h"
#include "VmsUserRepository.h"
#include "VmsUserService.h"
#include "StringHelpers.h"
#include "CameraManager.h"

#include <gst/gst.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <csignal>

#if defined(__unix__) || defined(__APPLE__)
#include <termios.h>
#include <unistd.h>
#endif

namespace {

volatile std::sig_atomic_t g_shutdown_requested = 0;

void handle_shutdown_signal(int signal_number) {
    // 1. signal handler에서는 종료 플래그만 설정
    if (signal_number == SIGINT || signal_number == SIGTERM) {
        g_shutdown_requested = 1;
    }
}

void printUsage(const char* program) {
    std::cerr
        << "Usage:\n"
        << "  " << program << "\n"
        << "  " << program << " create-admin --id <login-id> --name <name>\n";
}

bool parseCreateAdminArgs(
    int argc,
    char const* argv[],
    std::string& loginId,
    std::string& name) {
    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        if ((argument == "--id" || argument == "--name") && i + 1 < argc) {
            std::string& destination = argument == "--id" ? loginId : name;
            if (!destination.empty()) return false;
            destination = argv[++i];
        } else {
            return false;
        }
    }
    loginId = trim(loginId);
    name = trim(name);
    return !loginId.empty() && !name.empty();
}

bool readPasswordLine(const char* prompt, std::string& password) {
    std::cerr << prompt << std::flush;

#if defined(__unix__) || defined(__APPLE__)
    termios previous{};
    const bool terminal = isatty(STDIN_FILENO) == 1;
    if (terminal) {
        if (tcgetattr(STDIN_FILENO, &previous) != 0) return false;
        termios hidden = previous;
        hidden.c_lflag &= static_cast<tcflag_t>(~ECHO);
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden) != 0) return false;
    }
#endif

    const bool read = static_cast<bool>(std::getline(std::cin, password));

#if defined(__unix__) || defined(__APPLE__)
    if (terminal) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &previous);
        std::cerr << '\n';
    }
#endif
    return read;
}

int runCreateAdmin(int argc, char const* argv[], const AppRuntimeConfig& config) {
    std::string loginId;
    std::string name;
    if (!parseCreateAdminArgs(argc, argv, loginId, name)) {
        printUsage(argv[0]);
        return 2;
    }

    std::string password;
    std::string confirmation;
    if (!readPasswordLine("Password: ", password)
        || !readPasswordLine("Confirm password: ", confirmation)) {
        std::cerr << "Failed to read password.\n";
        return 2;
    }
    if (trim(password).empty()) {
        std::cerr << "Password must not be empty.\n";
        return 2;
    }
    if (password != confirmation) {
        std::cerr << "Passwords do not match.\n";
        return 2;
    }

    AppDb::configure(config.dbConninfo, config.dbMapperPath);
    const auto status = VmsAdminBootstrap{}.createAdmin(loginId, name, password);
    if (status == VmsAdminBootstrap::Status::Created) {
        std::cout << "Initial administrator '" << loginId << "' created.\n";
        return 0;
    }
    if (status == VmsAdminBootstrap::Status::AdminAlreadyExists) {
        std::cerr << "An administrator already exists; creation refused.\n";
        return 3;
    }
    if (status == VmsAdminBootstrap::Status::LoginIdAlreadyExists) {
        std::cerr << "Login ID already exists.\n";
        return 4;
    }
    if (status == VmsAdminBootstrap::Status::AdminRoleMissing) {
        std::cerr << "ADMIN role is missing; apply database migrations first.\n";
        return 5;
    }
    std::cerr << "Failed to create initial administrator.\n";
    return 1;
}

} // namespace

int main(int argc, char const* argv[]) {
    namespace fs = std::filesystem;
    const fs::path executableDirectory = fs::absolute(argv[0]).parent_path();
    const fs::path resourceDirectory =
        (executableDirectory / ".." / "resources").lexically_normal();

    AppRuntimeConfig config;
    if (!loadApplicationConfig(config, resourceDirectory.string(), "application.yml")) {
        return 1;
    }
    if (fs::path(config.dbMapperPath).is_relative()) {
        config.dbMapperPath = (executableDirectory / config.dbMapperPath).lexically_normal().string();
    }

    if (argc > 1) {
        if (std::string(argv[1]) == "create-admin") {
            return runCreateAdmin(argc, argv, config);
        }
        printUsage(argv[0]);
        return 2;
    }

    const std::string serverHost = config.serverHost;
    const int port = config.serverPort;
    const int server_threads = config.serverThreads;

    AppDb::configure(config.dbConninfo, config.dbMapperPath);

    // GStreamer 초기화
    gst_init(nullptr, nullptr);

    // CameraManager 생성
    CameraManager camera_menager;

    // 종료 신호 핸들러 등록
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);

    HttpServer server(serverHost, port, server_threads);
    VmsUserRepository userRepository;
    VmsUserService userService(userRepository);
    VmsAdminRepository adminRepository;
    VmsAdminService adminService(adminRepository);
    VmsDeviceRepository deviceRepository;
    VmsDeviceService deviceService(deviceRepository);
    VmsUserController userController(server, userService);
    VmsAdminController adminController(server, adminService, userService);
    VmsDeviceController deviceController(server, deviceService);

    ThreadPool onvifThreadPool;
    OnvifDeviceProvisioningService onvifProvisioning(
        onvifThreadPool,
        config.onvifDiscoveryIntervalMs);

    if (!server.start()) {
        return 1;
    }

    onvifProvisioning.start();

    // 종료 신호 대기
    while (g_shutdown_requested == 0) { g_usleep(100 * 1000); }

    // 종료 처리
    std::cout << std::endl;
    std::cout << "Shutdown signal received" << std::endl;
    std::cout << "Stopping CameraManager..." << std::endl;

    return 0;
}
