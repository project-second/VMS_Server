#include "ApplicationConfigLoader.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#include <yaml-cpp/yaml.h>

#include "LogSync.h"

namespace {

bool setEnvRequired(const YAML::Node& node, const char* env_key, const char* path_hint) {
    if (!node || !node.IsScalar()) {
        LogSync::error("[Config] missing required key: ", path_hint);
        return false;
    }

    const std::string value = node.as<std::string>();
    if (value.empty()) {
        LogSync::error("[Config] empty required key: ", path_hint);
        return false;
    }

    setenv(env_key, value.c_str(), 1);
    return true;
}

bool setEnvOptional(const YAML::Node& node, const char* env_key, const char* path_hint) {
    if (!node) {
        unsetenv(env_key);
        return true;
    }

    if (!node.IsScalar()) {
        LogSync::error("[Config] invalid optional key (scalar required): ", path_hint);
        return false;
    }

    const std::string value = node.as<std::string>();
    if (value.empty()) {
        unsetenv(env_key);
        return true;
    }

    setenv(env_key, value.c_str(), 1);
    return true;
}

} // namespace

bool loadApplicationConfig(const std::string& directory, const std::string& file) {
    const std::filesystem::path path{std::filesystem::current_path() / directory / file};

    if (!std::filesystem::exists(path)) {
        LogSync::error("[Config] resources/application.yml not found.");
        return false;
    }

    try {
        const YAML::Node root = YAML::LoadFile(path.string());
        const YAML::Node app = root["app"];
        if (!app) {
            LogSync::error("[Config] missing required key: app");
            return false;
        }

        bool ok = true;

        const YAML::Node db = app["db"];
        ok = setEnvRequired(db["conninfo"], "APP_DB_CONNINFO", "app.db.conninfo") && ok;
        ok = setEnvRequired(db["mapperPath"], "APP_DB_MAPPER_PATH", "app.db.mapperPath") && ok;

        const YAML::Node http = app["http"];
        const YAML::Node http_server = http["server"];
        ok = setEnvRequired(http_server["host"], "APP_HTTP_SERVER_HOST", "app.http.server.host") && ok;
        ok = setEnvRequired(http_server["port"], "APP_HTTP_SERVER_PORT", "app.http.server.port") && ok;
        ok = setEnvRequired(http_server["threads"], "APP_HTTP_SERVER_THREADS", "app.http.server.threads") && ok;

        const YAML::Node http_client_pool = http["clientPool"];
        ok = setEnvRequired(http_client_pool["threads"], "APP_HTTP_CLIENT_POOL_THREADS",
                 "app.http.clientPool.threads")
            && ok;
        ok = setEnvRequired(http_client_pool["maxQueue"], "APP_HTTP_CLIENT_POOL_MAX_QUEUE",
                 "app.http.clientPool.maxQueue")
            && ok;

        const YAML::Node onvif = app["onvif"];
        ok = setEnvOptional(onvif["offlineStaleSeconds"], "APP_ONVIF_OFFLINE_STALE_SECONDS",
                 "app.onvif.offlineStaleSeconds")
            && ok;
        const YAML::Node discovery = onvif["discovery"];
        ok = setEnvRequired(discovery["multicastIp"], "APP_ONVIF_DISCOVERY_MCAST_IP",
                 "app.onvif.discovery.multicastIp")
            && ok;
        ok = setEnvRequired(discovery["multicastPort"], "APP_ONVIF_DISCOVERY_MCAST_PORT",
                 "app.onvif.discovery.multicastPort")
            && ok;
        ok = setEnvRequired(discovery["recvTimeoutMs"], "APP_ONVIF_DISCOVERY_RECV_TIMEOUT_MS",
                 "app.onvif.discovery.recvTimeoutMs")
            && ok;
        ok = setEnvRequired(discovery["intervalMs"], "APP_ONVIF_DISCOVERY_INTERVAL_MS",
                 "app.onvif.discovery.intervalMs")
            && ok;
        ok = setEnvOptional(discovery["interfaceIp"], "APP_ONVIF_DISCOVERY_INTERFACE_IP",
                 "app.onvif.discovery.interfaceIp")
            && ok;
        ok = setEnvOptional(discovery["probeRetryCount"], "APP_ONVIF_DISCOVERY_PROBE_RETRY_COUNT",
                 "app.onvif.discovery.probeRetryCount")
            && ok;
        ok = setEnvOptional(discovery["probeRetryIntervalMs"], "APP_ONVIF_DISCOVERY_PROBE_RETRY_INTERVAL_MS",
                 "app.onvif.discovery.probeRetryIntervalMs")
            && ok;
        ok = setEnvOptional(discovery["unicastTargets"], "APP_ONVIF_DISCOVERY_UNICAST_TARGETS",
                 "app.onvif.discovery.unicastTargets")
            && ok;

        const YAML::Node defaults = onvif["defaults"];
        ok = setEnvRequired(defaults["deviceServicePath"], "APP_ONVIF_DEFAULT_DEVICE_SERVICE_PATH",
                 "app.onvif.defaults.deviceServicePath")
            && ok;
        ok = setEnvRequired(defaults["mediaServicePath"], "APP_ONVIF_DEFAULT_MEDIA_SERVICE_PATH",
                 "app.onvif.defaults.mediaServicePath")
            && ok;
        ok = setEnvRequired(defaults["eventServicePath"], "APP_ONVIF_DEFAULT_EVENT_SERVICE_PATH",
                 "app.onvif.defaults.eventServicePath")
            && ok;

        const YAML::Node pull_messages = onvif["pullMessages"];
        ok = setEnvRequired(pull_messages["maxPollCount"], "APP_ONVIF_PULL_MESSAGES_MAX_POLL",
                 "app.onvif.pullMessages.maxPollCount")
            && ok;

        const YAML::Node cctv = app["cctv"];
        ok = setEnvRequired(cctv["id"], "APP_CCTV_ID", "app.cctv.id") && ok;
        ok = setEnvRequired(cctv["pw"], "APP_CCTV_PW", "app.cctv.pw") && ok;

        const YAML::Node mediamtx = app["mediamtx"];
        ok = setEnvRequired(mediamtx["apiHost"], "APP_MEDIAMTX_API_HOST", "app.mediamtx.apiHost") && ok;
        ok = setEnvRequired(mediamtx["apiPort"], "APP_MEDIAMTX_API_PORT", "app.mediamtx.apiPort") && ok;
        ok = setEnvRequired(mediamtx["playbackHost"], "APP_MEDIAMTX_PLAYBACK_HOST",
                 "app.mediamtx.playbackHost")
            && ok;
        ok = setEnvRequired(mediamtx["playbackPort"], "APP_MEDIAMTX_PLAYBACK_PORT",
                 "app.mediamtx.playbackPort")
            && ok;
        ok = setEnvRequired(mediamtx["playbackSyncIntervalMs"], "APP_MEDIAMTX_PLAYBACK_SYNC_INTERVAL_MS",
                 "app.mediamtx.playbackSyncIntervalMs")
            && ok;
        ok = setEnvRequired(mediamtx["rtspHost"], "APP_MEDIAMTX_RTSP_HOST", "app.mediamtx.rtspHost") && ok;
        ok = setEnvRequired(mediamtx["rtspPort"], "APP_MEDIAMTX_RTSP_PORT", "app.mediamtx.rtspPort") && ok;

        if (!ok) return false;

        LogSync::info("[Config] loaded: ", path.string());
        return true;
    } catch (const std::exception& e) {
        LogSync::error("[Config] failed to load application.yml: ", e.what());
        return false;
    }
}
