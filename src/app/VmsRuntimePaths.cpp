#include "app/VmsRuntimePaths.h"

#include <cstdlib>

VmsRuntimePaths resolveRuntimePaths(const char* argv0) {
    namespace fs = std::filesystem;
    const fs::path executableDirectory = fs::absolute(argv0 == nullptr ? "" : argv0).parent_path();
    return VmsRuntimePaths{
        .executableDirectory = executableDirectory,
        .resourceDirectory = (executableDirectory / ".." / "resources").lexically_normal(),
    };
}

void normalizeRuntimeConfigPaths(AppRuntimeConfig& config, const VmsRuntimePaths& paths) {
    namespace fs = std::filesystem;
    if (fs::path(config.dbMapperPath).is_relative()) {
        config.dbMapperPath = (paths.executableDirectory / config.dbMapperPath).lexically_normal().string();
    }
}

void configurePackagedGStreamerEnvironment(const VmsRuntimePaths& paths) {
    const std::filesystem::path installRoot = paths.executableDirectory.parent_path();
    const std::filesystem::path pluginPath = installRoot / "lib" / "gstreamer-1.0";
    const std::filesystem::path scannerPath = installRoot / "libexec" / "gstreamer-1.0" / "gst-plugin-scanner";

    if (std::filesystem::exists(pluginPath)) {
        setenv("GST_PLUGIN_PATH", pluginPath.c_str(), 1);
    }
    if (std::filesystem::exists(scannerPath)) {
        setenv("GST_PLUGIN_SCANNER", scannerPath.c_str(), 1);
    }
}
