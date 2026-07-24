#include "MediaService.h"

#include "LogSync.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>

namespace {

std::string fallbackIfEmpty(const std::string& value, const std::string& fallback) {
    return value.empty() ? fallback : value;
}

std::string sanitizePathPart(const std::string& value) {
    std::string result;
    result.reserve(value.size());

    for (const unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_') {
            result.push_back(static_cast<char>(ch));
        } else {
            result.push_back('_');
        }
    }

    while (!result.empty() && result.front() == '_') {
        result.erase(result.begin());
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }

    return fallbackIfEmpty(result, "unknown");
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

std::string makeCameraId(
    const ProvisioningResult& result,
    const OnvifProvisionedProfile& profile,
    std::size_t index,
    std::int64_t channelId) {
    const std::string deviceId = sanitizePathPart(deviceNameOf(result));
    const std::string profileId = !profile.profile.token.empty()
        ? sanitizePathPart(profile.profile.token)
        : "profile_" + std::to_string(index);
    return deviceId + "__ch" + std::to_string(channelId) + "__" + profileId;
}

std::string makeRecordPattern(const MediaRecordingOptions& options, const std::string& cameraId) {
    const std::filesystem::path directory = std::filesystem::path(options.recordRootDir) / cameraId;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        LogSync::error("[Media] failed to create recording directory: ", directory.string(), " - ", error.message());
    }

    return (directory / "%05d.mp4").string();
}

} // namespace

MediaService::MediaService(
    CameraManager& cm,
    RtspRelayServer& relay,
    OnvifProvisioningRepository& rp,
    MediaRecordingOptions recordingOptions)
    : cameraManager{cm},
      relayServer{relay},
      repository{rp},
      recordingOptions_{std::move(recordingOptions)} {}

MediaService::~MediaService() {}

void MediaService::addCamera(const std::vector<ProvisioningResult>& results) {
    for (const ProvisioningResult& result : results) {
        const std::optional<std::int64_t> channelId = repository.ensureChannel(result);
        if (!channelId) {
            LogSync::error("[Provisioning] failed to upsert device/channel: ",
                result.discoveredDevice.endpointReference);
            continue;
        }

        if (!repository.upsertProfiles(*channelId, result)) {
            LogSync::error("[Provisioning] failed to upsert profiles: channel_id=", *channelId);
            continue;
        }

        std::string channelRelayUrl;

        for (std::size_t index = 0; index < result.profiles.size(); ++index) {
            const OnvifProvisionedProfile& profile = result.profiles[index];
            if (!profile.streamUri || profile.streamUri->uri.empty()) {
                LogSync::error("[Media] skip ONVIF profile without RTSP URI: device=",
                    result.discoveredDevice.endpointReference,
                    ", profile=",
                    profile.profile.token);
                continue;
            }

            CameraConfig config;
            const auto profileId = repository.findProfileId(*channelId, profile.profile.token);
            if (!profileId) {
                LogSync::error("[Media] profile_id not found: channel_id=",
                    *channelId, ", token=", profile.profile.token);
                continue;
            }
            config.profile_id = *profileId;
            config.camera_id = makeCameraId(result, profile, index, *channelId);
            config.rtsp_uri = profile.streamUri->uri;
            config.record_pattern = makeRecordPattern(recordingOptions_, config.camera_id);
            config.split_seconds = recordingOptions_.splitSeconds;

            if (relayServer.is_enabled()) {
                if (relayServer.add_h264_camera(config)) {
                    if (channelRelayUrl.empty()) {
                        channelRelayUrl = relayServer.make_public_url(config.camera_id);
                    }
                } else {
                    LogSync::error("[Media] failed to register RTSP relay: ", config.camera_id);
                }
            }

            bool alreadyRegistered = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                alreadyRegistered = registeredCameraIds_.find(config.camera_id) != registeredCameraIds_.end();
            }

            if (alreadyRegistered) {
                continue;
            }

            if (!cameraManager.add_camera(config)) {
                LogSync::error("[Media] failed to register camera: ", config.camera_id);
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                registeredCameraIds_.insert(config.camera_id);
            }

            LogSync::info("[Media] registered ONVIF camera: ", config.camera_id);
        }

        if (!channelRelayUrl.empty() &&
            !repository.updateChannelServiceRtspUrl(*channelId, channelRelayUrl)) {
            LogSync::error("[Provisioning] failed to update channel relay URL: channel_id=", *channelId);
        }

    }
}
