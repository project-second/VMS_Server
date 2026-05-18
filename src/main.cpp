#include "ApplicationConfigLoader.h"

int main(int argc, char const* argv[]) {
    AppRuntimeConfig config;
    if (!loadApplicationConfig(config)) {
        return 1;
    }

    const int port = config.serverPort;
    const int server_threads = config.serverThreads;
    const int client_pool_threads = config.clientPoolThreads;
    const int client_pool_max_queue = config.clientPoolMaxQueue;

    return 0;
}
