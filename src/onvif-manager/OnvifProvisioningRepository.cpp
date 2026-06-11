#include "OnvifProvisioningRepository.h"

#include "DbRowHelpers.h"
#include "DbRuntime.hpp"
#include "LogSync.h"

#include <cstdint>
#include <string>

namespace {

constexpr std::int64_t kDeviceTypeCctv = 1;
constexpr std::int64_t kOnlineStateOnline = 1;
constexpr std::int64_t kVideoStateUnknown = 0;
constexpr std::int64_t kDefaultChannelNo = 1;

std::string firstStreamUri(const ProvisioningResult& result) {
    for (const OnvifProvisionedProfile& profile : result.profiles) {
        if (profile.streamUri && !profile.streamUri->uri.empty()) {
            return profile.streamUri->uri;
        }
    }
    return {};
}

std::string deviceNameOf(const ProvisioningResult& result) {
    if (!result.deviceInformation.model.empty()) {
        return result.deviceInformation.model;
    }
    if (!result.discoveredDevice.endpointReference.empty()) {
        return result.discoveredDevice.endpointReference;
    }
    return result.deviceServiceUrl;
}

std::optional<std::int64_t> firstId(const DbTemplate::TypedRowSet& rows, const std::string& column) {
    if (rows.empty()) {
        return std::nullopt;
    }

    std::int64_t id = 0;
    if (!DbRow::getInt64Cell(rows.front(), column, id)) {
        return std::nullopt;
    }
    return id;
}

} // namespace

OnvifProvisioningRepository::OnvifProvisioningRepository() = default;

OnvifProvisioningRepository::~OnvifProvisioningRepository() = default;

bool OnvifProvisioningRepository::upsert(const std::vector<ProvisioningResult>& results) {
    bool ok = true;

    for (const ProvisioningResult& result : results) {
        std::optional<std::int64_t> deviceId = upsertDevice(result);
        if (!deviceId) {
            ok = false;
            continue;
        }

        std::optional<std::int64_t> channelId = upsertChannel(*deviceId, result);
        if (!channelId) {
            ok = false;
            continue;
        }

        if (!upsertProfiles(*channelId, result)) {
            ok = false;
        }
    }

    return ok;
}

std::optional<std::int64_t> OnvifProvisioningRepository::upsertDevice(const ProvisioningResult& result) {
    const std::string uuid = result.discoveredDevice.endpointReference;
    if (uuid.empty()) {
        LogSync::error("[ONVIF] repository skipped device without endpointReference");
        return std::nullopt;
    }

    DbTemplate::ParamMap selectParams;
    selectParams.emplace("uuid", uuid);
    auto rows = AppDb::select("onvif.getDeviceByUuid", selectParams);

    DbTemplate::ParamMap params;
    params.emplace("type", kDeviceTypeCctv);
    params.emplace("uuid", uuid);
    params.emplace("addr", result.deviceServiceUrl);
    params.emplace("name", deviceNameOf(result));
    params.emplace("manufacturer", result.deviceInformation.manufacturer);
    params.emplace("model", result.deviceInformation.model);
    params.emplace("username", std::monostate{});
    params.emplace("password_enc", std::monostate{});

    std::optional<std::int64_t> deviceId = firstId(rows, "device_id");
    if (deviceId) {
        params.emplace("device_id", *deviceId);
        if (!AppDb::update("onvif.updateDevice", params)) {
            return std::nullopt;
        }
    } else {
        if (!AppDb::insert("onvif.insertDevice", params)) {
            return std::nullopt;
        }

        rows = AppDb::select("onvif.getDeviceByUuid", selectParams);
        deviceId = firstId(rows, "device_id");
        if (!deviceId) {
            return std::nullopt;
        }
    }

    DbTemplate::ParamMap healthParams;
    healthParams.emplace("device_id", *deviceId);
    healthParams.emplace("online_state", kOnlineStateOnline);
    healthParams.emplace("video_state", kVideoStateUnknown);
    healthParams.emplace("rtt_ms", std::monostate{});
    healthParams.emplace("rssi", std::monostate{});
    if (!AppDb::insert("onvif.upsertDeviceHealth", healthParams)) {
        LogSync::error("[ONVIF] repository failed to upsert device health for device_id=", *deviceId);
    }

    return deviceId;
}

std::optional<std::int64_t> OnvifProvisioningRepository::upsertChannel(
    std::int64_t deviceId,
    const ProvisioningResult& result) {
    DbTemplate::ParamMap selectParams;
    selectParams.emplace("device_id", deviceId);
    selectParams.emplace("channel_no", kDefaultChannelNo);
    auto rows = AppDb::select("onvif.getChannelByDeviceAndNo", selectParams);

    DbTemplate::ParamMap params;
    params.emplace("device_id", deviceId);
    params.emplace("channel_no", kDefaultChannelNo);
    params.emplace("name", deviceNameOf(result));
    params.emplace("use_profile", std::int64_t(1));
    params.emplace("service_rtsp_url", firstStreamUri(result));

    std::optional<std::int64_t> channelId = firstId(rows, "channel_id");
    if (channelId) {
        params.emplace("channel_id", *channelId);
        if (!AppDb::update("onvif.updateChannel", params)) {
            return std::nullopt;
        }
    } else {
        if (!AppDb::insert("onvif.insertChannel", params)) {
            return std::nullopt;
        }

        rows = AppDb::select("onvif.getChannelByDeviceAndNo", selectParams);
        channelId = firstId(rows, "channel_id");
    }

    return channelId;
}

bool OnvifProvisioningRepository::upsertProfiles(std::int64_t channelId, const ProvisioningResult& result) {
    bool ok = true;
    std::int64_t profileNo = 1;

    for (const OnvifProvisionedProfile& provisionedProfile : result.profiles) {
        const std::string token = provisionedProfile.profile.token;
        if (token.empty()) {
            continue;
        }

        DbTemplate::ParamMap selectParams;
        selectParams.emplace("channel_id", channelId);
        selectParams.emplace("onvif_profile_token", token);
        auto rows = AppDb::select("onvif.getProfileByChannelAndToken", selectParams);

        DbTemplate::ParamMap params;
        params.emplace("channel_id", channelId);
        params.emplace("profile_no", profileNo);
        params.emplace("name", provisionedProfile.profile.name);
        params.emplace("onvif_profile_token", token);
        params.emplace("video_codec", std::string{});
        params.emplace("audio_codec", std::string{});
        params.emplace("cctv_rtsp_url", provisionedProfile.streamUri ? provisionedProfile.streamUri->uri : std::string{});

        std::optional<std::int64_t> profileId = firstId(rows, "profile_id");
        if (profileId) {
            params.emplace("profile_id", *profileId);
            if (!AppDb::update("onvif.updateProfile", params)) {
                ok = false;
            }
        } else if (!AppDb::insert("onvif.insertProfile", params)) {
            ok = false;
        }

        ++profileNo;
    }

    return ok;
}
