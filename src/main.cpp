#include "ApplicationConfigLoader.h"

#include "DbRuntime.hpp"
#include "HttpServer.h"
#include "VmsUserController.h"
#include "VmsUserService.h"

#include <chrono>
#include <thread>

int main(int argc, char const* argv[]) {
    AppRuntimeConfig config;
    if (!loadApplicationConfig(config, "resources", "application.yml")) {
        return 1;
    }

    const std::string serverHost = config.serverHost;
    const int port = config.serverPort;
    const int server_threads = config.serverThreads;

    AppDb::configure(config.dbConninfo, config.dbMapperPath);

    HttpServer server(serverHost, port, server_threads);
    VmsUserService userService;
    VmsUserController userController(server, userService);

    if (!server.start()) {
        return 1;
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::hours(24));
    }

    return 0;
}
