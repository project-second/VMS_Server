#ifndef ONVIF_PROVISIONING_TYPES_H
#define ONVIF_PROVISIONING_TYPES_H

#include "OnvifDeviceClient.h"
#include "OnvifDiscoveryService.h"
#include "OnvifMediaClient.h"

#include <optional>
#include <string>
#include <vector>

struct OnvifProvisionedProfile {
    OnvifMediaProfile profile;
    std::optional<OnvifStreamUri> streamUri;
};

struct ProvisioningResult {
    OnvifDiscoveredDevice discoveredDevice;

    std::string deviceServiceUrl;
    std::string mediaServiceUrl;

    OnvifDeviceInformation deviceInformation;
    OnvifDeviceCapabilities capabilities;

    std::vector<OnvifProvisionedProfile> profiles;
};

#endif // ONVIF_PROVISIONING_TYPES_H
