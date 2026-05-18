#include "AppRuntimeConfigLoader.h"

#include "EnvManager.h"
#include "LogSync.h"

bool AppRuntimeConfigLoader::load(AppRuntimeConfig& config) const {
    const auto server_host = readEnvStringStrict("APP_HTTP_SERVER_HOST");
    if (!server_host) {
        LogSync::error("[Config] invalid APP_HTTP_SERVER_HOST.");
        return false;
    }

    config.serverHost = *server_host;

    return readRequiredEnvInt("APP_HTTP_SERVER_PORT", config.serverPort)
        && readRequiredEnvInt("APP_HTTP_SERVER_THREADS", config.serverThreads)
        && readRequiredEnvInt("APP_HTTP_CLIENT_POOL_THREADS", config.clientPoolThreads)
        && readRequiredEnvInt("APP_HTTP_CLIENT_POOL_MAX_QUEUE", config.clientPoolMaxQueue)
        && readRequiredEnvInt("APP_ONVIF_DISCOVERY_INTERVAL_MS", config.onvifDiscoveryIntervalMs)
        && readRequiredEnvInt("APP_MEDIAMTX_PLAYBACK_SYNC_INTERVAL_MS", config.mediaMtxPlaybackSyncIntervalMs);
}

bool AppRuntimeConfigLoader::readRequiredEnvInt(const char* key, int& target) const {
    if (readEnvIntStrict(key, target)) {
        return true;
    }

    LogSync::error("[Config] invalid ", key, ".");
    return false;
}
