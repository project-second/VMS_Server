#include "app/VmsRuntimePaths.h"

#include <cstdlib>
#include <string>

namespace {

void prependEnvironmentPath(const char* name, const std::filesystem::path& path) {
    std::string value = path.string();
    const char* current = std::getenv(name);
    if (current != nullptr && *current != '\0') {
        value += ":";
        value += current;
    }
    setenv(name, value.c_str(), 1);
}

} // namespace

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
    const std::filesystem::path libraryPath = installRoot / "lib";
    const std::filesystem::path pluginPath = installRoot / "lib" / "gstreamer-1.0";
    const std::filesystem::path scannerPath = installRoot / "libexec" / "gstreamer-1.0" / "gst-plugin-scanner";

    // gst-plugin-scanner는 별도 process로 실행되므로 vms_server의 RPATH를 상속하지 않는다.
    // child loader가 패키지 내부 GStreamer shared library를 찾도록 환경에 lib 경로를 전달한다.
    if (std::filesystem::exists(libraryPath)) {
        prependEnvironmentPath("LD_LIBRARY_PATH", libraryPath);
    }
    if (std::filesystem::exists(pluginPath)) {
        setenv("GST_PLUGIN_PATH", pluginPath.c_str(), 1);
        setenv("GST_PLUGIN_PATH_1_0", pluginPath.c_str(), 1);
    }
    if (std::filesystem::exists(scannerPath)) {
        setenv("GST_PLUGIN_SCANNER", scannerPath.c_str(), 1);
    }
}
