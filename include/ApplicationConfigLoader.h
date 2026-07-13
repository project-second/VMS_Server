#ifndef APPLICATION_CONFIG_LOADER_H
#define APPLICATION_CONFIG_LOADER_H

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
    bool onvifSoapLogEnabled = false;
    std::string onvifSoapLogDir = "/tmp";
    std::string mediaRecordingRootDir = "recordings/onvif";
    int mediaRecordingSplitSeconds = 5;
    bool mediaRtspRelayEnabled = true;
    std::string mediaRtspRelayBindAddress = "0.0.0.0";
    int mediaRtspRelayPort = 8555;
    std::string mediaRtspRelayAdvertisedHost = "127.0.0.1";
    int mediaMtxPlaybackSyncIntervalMs = 30000;
};

bool loadApplicationConfig(
    AppRuntimeConfig& config,
    const std::string& directory = "resources",
    const std::string& file = "application.yml");

#endif // APPLICATION_CONFIG_LOADER_H
