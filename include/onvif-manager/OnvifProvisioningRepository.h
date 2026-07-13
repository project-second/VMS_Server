#ifndef __ONVIFPROVISIONINGREPOSITORY_H__
#define __ONVIFPROVISIONINGREPOSITORY_H__

#include "OnvifProvisioningTypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// ONVIF provisioning 결과를 DB schema에 반영하는 저장소다.
//
// Service는 ONVIF 통신과 결과 수집만 담당하고, 이 class가 devices,
// device_health, channels, profiles 테이블에 맞춰 저장한다. 현재 app-db가 단건
// statement 중심이므로 내부 구현은 select 후 insert/update 방식이다. 추후 app-db에
// transaction/bulk API가 들어오면 이 class의 내부만 bulk upsert로 교체하면 된다.
class OnvifProvisioningRepository {
public:
    OnvifProvisioningRepository();
    ~OnvifProvisioningRepository();

    // devices/device_health/channel을 먼저 upsert하고 relay mount에 사용할 channel_id를 반환한다.
    std::optional<std::int64_t> ensureChannel(const ProvisioningResult& result);

    // channel_id가 포함된 relay URL을 channels.service_rtsp_url에 반영한다.
    bool updateChannelServiceRtspUrl(std::int64_t channelId, const std::string& serviceRtspUrl);

    // channel 아래의 ONVIF media profiles를 profiles 테이블에 반영한다.
    bool upsertProfiles(std::int64_t channelId, const ProvisioningResult& result);

private:
    // devices와 device_health를 갱신하고 devices.device_id를 반환한다.
    std::optional<std::int64_t> upsertDevice(const ProvisioningResult& result);

    // 현재는 ONVIF 장비 1대를 기본 channel_no=1인 CCTV 채널 하나로 매핑한다.
    std::optional<std::int64_t> upsertChannel(std::int64_t deviceId, const ProvisioningResult& result);

};

#endif // __ONVIFPROVISIONINGREPOSITORY_H__
