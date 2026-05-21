#include "ApplicationConfigLoader.h"

#include "HttpServer.h"

int main(int argc, char const* argv[]) {
    AppRuntimeConfig config;
    if (!loadApplicationConfig(config, "resources", "application.yml")) {
        return 1;
    }

    const std::string serverHost = config.serverHost;
    const int port = config.serverPort;
    const int server_threads = config.serverThreads;
    const int client_pool_threads = config.clientPoolThreads;
    const int client_pool_max_queue = config.clientPoolMaxQueue;


    HttpServer server(serverHost, port, server_threads);

    

    server.start();

    return 0;
}
