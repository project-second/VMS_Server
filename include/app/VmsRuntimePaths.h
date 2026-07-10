#ifndef VMS_RUNTIME_PATHS_H
#define VMS_RUNTIME_PATHS_H

#include "ApplicationConfigLoader.h"

#include <filesystem>

struct VmsRuntimePaths {
    std::filesystem::path executableDirectory;
    std::filesystem::path resourceDirectory;
};

VmsRuntimePaths resolveRuntimePaths(const char* argv0);
void normalizeRuntimeConfigPaths(AppRuntimeConfig& config, const VmsRuntimePaths& paths);
void configurePackagedGStreamerEnvironment(const VmsRuntimePaths& paths);

#endif // VMS_RUNTIME_PATHS_H
