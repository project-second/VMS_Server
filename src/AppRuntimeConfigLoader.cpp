#include "AppRuntimeConfigLoader.h"

#include "ApplicationConfigLoader.h"

bool AppRuntimeConfigLoader::load(AppRuntimeConfig& config) const {
    return loadApplicationConfig(config);
}
