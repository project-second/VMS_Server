#include "ApplicationConfigLoader.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "LogSync.h"

namespace {

struct Binding {
    const char* path;
    bool required;
    std::optional<std::string> default_value;
    std::string AppRuntimeConfig::* string_field;
    int AppRuntimeConfig::* int_field;
};

YAML::Node findChild(const YAML::Node& node, const std::string& key) {
    if (!node || !node.IsDefined() || node.IsNull() || !node.IsMap()) {
        return YAML::Node{};
    }

    for (YAML::const_iterator it = node.begin(); it != node.end(); ++it) {
        if (it->first.IsScalar() && it->first.as<std::string>() == key) {
            return it->second;
        }
    }

    return YAML::Node{};
}

YAML::Node findNodePart(const YAML::Node& node, const std::string& path, std::size_t start) {
    const std::size_t dot = path.find('.', start);
    const std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
    const YAML::Node child = findChild(node, key);

    if (dot == std::string::npos) {
        return child;
    }

    return findNodePart(child, path, dot + 1);
}

YAML::Node findNode(const YAML::Node& root, const char* dotted_path) {
    return findNodePart(root, std::string(dotted_path), 0);
}

bool missing(const YAML::Node& node) {
    return !node || !node.IsDefined() || node.IsNull();
}

bool parseInt(const std::string& value, int& out) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed, 10);
        if (consumed != value.size() || parsed <= 0) {
            return false;
        }
        out = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool applyRuntimeField(const Binding& binding, const std::string& value, AppRuntimeConfig& config) {
    if (binding.string_field != nullptr) {
        config.*(binding.string_field) = value;
        return true;
    }

    if (binding.int_field != nullptr) {
        int parsed = 0;
        if (!parseInt(value, parsed)) {
            LogSync::error("[Config] invalid integer key: ", binding.path);
            return false;
        }
        config.*(binding.int_field) = parsed;
    }

    return true;
}

bool applyBinding(const YAML::Node& root, const Binding& binding, AppRuntimeConfig& config) {
    const YAML::Node node = findNode(root, binding.path);

    if (missing(node)) {
        if (binding.default_value) {
            return applyRuntimeField(binding, *binding.default_value, config);
        }

        if (binding.required) {
            LogSync::error("[Config] missing required key: ", binding.path);
            return false;
        }

        return true;
    }

    if (!node.IsScalar()) {
        LogSync::error("[Config] invalid key (scalar required): ", binding.path);
        return false;
    }

    const std::string value = node.as<std::string>();
    if (value.empty()) {
        if (binding.required) {
            LogSync::error("[Config] empty required key: ", binding.path);
            return false;
        }
        return true;
    }

    return applyRuntimeField(binding, value, config);
}

const std::vector<Binding>& bindings() {
    static const std::vector<Binding> items = {
        {"app.db.conninfo", true, std::nullopt, &AppRuntimeConfig::dbConninfo, nullptr},
        {"app.db.mapperPath", false, "resources/db", &AppRuntimeConfig::dbMapperPath, nullptr},

        {"app.http.server.host", false, "0.0.0.0", &AppRuntimeConfig::serverHost, nullptr},
        {"app.http.server.port", false, "8080", nullptr, &AppRuntimeConfig::serverPort},
        {"app.http.server.threads", false, "8", nullptr, &AppRuntimeConfig::serverThreads},
        {"app.http.clientPool.threads", false, "8", nullptr, &AppRuntimeConfig::clientPoolThreads},
        {"app.http.clientPool.maxQueue", false, "500", nullptr, &AppRuntimeConfig::clientPoolMaxQueue},

        {"app.onvif.discovery.intervalMs", false, "300000", nullptr,
            &AppRuntimeConfig::onvifDiscoveryIntervalMs},
        {"app.mediamtx.playbackSyncIntervalMs", false, "30000", nullptr,
            &AppRuntimeConfig::mediaMtxPlaybackSyncIntervalMs},
    };

    return items;
}

} // namespace

bool loadApplicationConfig(AppRuntimeConfig& config, const std::string& directory, const std::string& file) {
    const std::filesystem::path path{std::filesystem::current_path() / directory / file};

    if (!std::filesystem::exists(path)) {
        LogSync::error("[Config] ", path.string(), " not found.");
        return false;
    }

    try {
        const YAML::Node root = YAML::LoadFile(path.string());
        if (missing(findNode(root, "app"))) {
            LogSync::error("[Config] missing required key: app");
            return false;
        }

        bool ok = true;
        for (const Binding& binding : bindings()) {
            ok = applyBinding(root, binding, config) && ok;
        }

        if (!ok) return false;

        LogSync::info("[Config] loaded: ", path.string());
        return true;
    } catch (const std::exception& e) {
        LogSync::error("[Config] failed to load application.yml: ", e.what());
        return false;
    }
}
