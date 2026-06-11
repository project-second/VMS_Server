#ifndef __ONVIFPROVISIONINGREPOSITORY_H__
#define __ONVIFPROVISIONINGREPOSITORY_H__

#include "OnvifProvisioningTypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class OnvifProvisioningRepository {
public:
    OnvifProvisioningRepository();
    ~OnvifProvisioningRepository();

    bool upsert(const std::vector<ProvisioningResult>& results);

private:
    std::optional<std::int64_t> upsertDevice(const ProvisioningResult& result);
    std::optional<std::int64_t> upsertChannel(std::int64_t deviceId, const ProvisioningResult& result);
    bool upsertProfiles(std::int64_t channelId, const ProvisioningResult& result);
};

#endif // __ONVIFPROVISIONINGREPOSITORY_H__
