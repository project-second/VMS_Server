#ifndef ONVIF_PROVISIONING_TYPES_H
#define ONVIF_PROVISIONING_TYPES_H

#include "OnvifDeviceClient.h"
#include "OnvifDiscoveryService.h"
#include "OnvifMediaClient.h"

#include <optional>
#include <string>
#include <vector>

// ONVIF profile 하나와 해당 profile로 조회한 media URI를 묶은 DTO다.
// GetStreamUri는 profile별로 실패할 수 있으므로 streamUri는 optional로 둔다.
struct OnvifProvisionedProfile {
    OnvifMediaProfile profile;
    std::optional<OnvifStreamUri> streamUri;
};

// 장비 1대에 대한 provisioning/sync 결과다.
// Repository가 DB upsert에 필요한 값을 한 번에 받을 수 있도록 Discovery, Device,
// Media 결과를 이 구조체 하나로 모은다.
struct ProvisioningResult {
    // WS-Discovery에서 얻은 원본 장비 정보. endpointReference는 현재 devices.uuid로 사용한다.
    OnvifDiscoveredDevice discoveredDevice;

    // 실제 SOAP 호출에 사용한 endpoint URL들이다.
    std::string deviceServiceUrl;
    std::string mediaServiceUrl;

    // Device Service 호출 결과다.
    OnvifDeviceInformation deviceInformation;
    OnvifDeviceCapabilities capabilities;

    // Media Service 호출 결과다. profile token과 RTSP URI를 profile 단위로 보관한다.
    std::vector<OnvifProvisionedProfile> profiles;
};

#endif // ONVIF_PROVISIONING_TYPES_H
