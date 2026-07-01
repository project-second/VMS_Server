#pragma once

#include "OnvifDiscoveryService.h"

#include <vector>

std::vector<OnvifDiscoveredDevice> discoverOnvifDevicesCompat(const OnvifDiscoveryOptions& options);
