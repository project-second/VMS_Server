#ifndef APPLICATION_CONFIG_LOADER_H
#define APPLICATION_CONFIG_LOADER_H

#include <string>

#include "AppRuntimeConfigLoader.h"

bool loadApplicationConfig(
    AppRuntimeConfig& config,
    const std::string& directory = "resources",
    const std::string& file = "application.yml");

#endif // APPLICATION_CONFIG_LOADER_H
