#ifndef VMS_CREATE_ADMIN_COMMAND_H
#define VMS_CREATE_ADMIN_COMMAND_H

#include "ApplicationConfigLoader.h"

void printVmsUsage(const char* program);
int runCreateAdminCommand(int argc, char const* argv[], const AppRuntimeConfig& config);

#endif // VMS_CREATE_ADMIN_COMMAND_H
