#include "ApplicationConfigLoader.h"

#include "DbRuntime.hpp"
#include "HttpServer.h"
#include "OnvifDeviceProvisioningService.h"
#include "ThreadPool.hpp"
#include "VmsAdminController.h"
#include "VmsAdminRepository.h"
#include "VmsAdminService.h"
#include "VmsUserController.h"
#include "VmsUserRepository.h"
#include "VmsUserService.h"

#include <chrono>
#include <thread>

int main(int argc, char const* argv[]) {
    AppRuntimeConfig config;
    if (!loadApplicationConfig(config, "../resources", "application.yml")) {
        return 1;
    }

    const std::string serverHost = config.serverHost;
    const int port = config.serverPort;
    const int server_threads = config.serverThreads;

    AppDb::configure(config.dbConninfo, config.dbMapperPath);

    HttpServer server(serverHost, port, server_threads);
    VmsUserRepository userRepository;
    VmsUserService userService(userRepository);
    VmsAdminRepository adminRepository;
    VmsAdminService adminService(adminRepository);
    VmsUserController userController(server, userService);
    VmsAdminController adminController(server, adminService, userService);

    ThreadPool onvifThreadPool;
    OnvifDeviceProvisioningService onvifProvisioning(
        onvifThreadPool,
        config.onvifDiscoveryIntervalMs);

    if (!server.start()) {
        return 1;
    }

    onvifProvisioning.start();

    while (true) {
        std::this_thread::sleep_for(std::chrono::hours(24));
    }

    return 0;
}
