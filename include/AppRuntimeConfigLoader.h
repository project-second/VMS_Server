#ifndef APP_RUNTIME_CONFIG_LOADER_H
#define APP_RUNTIME_CONFIG_LOADER_H

#include <string>

struct AppRuntimeConfig {
    std::string dbConninfo;
    std::string dbMapperPath = "resources/db";

    std::string serverHost = "0.0.0.0";
    int serverPort = 8080;
    int serverThreads = 8;
    int clientPoolThreads = 8;
    int clientPoolMaxQueue = 500;
    int onvifDiscoveryIntervalMs = 300000;
    int mediaMtxPlaybackSyncIntervalMs = 30000;
};

class AppRuntimeConfigLoader {
public:
    bool load(AppRuntimeConfig& config) const;
};

#endif // APP_RUNTIME_CONFIG_LOADER_H
