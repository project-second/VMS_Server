#include "MediaService.h"

#include "LogSync.h"

#include <algorithm>
#include <cctype>
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

std::string makeDeviceId(const ProvisioningResult& result) {
    if (!result.discoveredDevice.endpointReference.empty()) {
        return sanitizePathPart(result.discoveredDevice.endpointReference);
    }
    if (!result.deviceServiceUrl.empty()) {
        return sanitizePathPart(result.deviceServiceUrl);
    }
    if (!result.mediaServiceUrl.empty()) {
        return sanitizePathPart(result.mediaServiceUrl);
    }
    return "unknown-device";
}

std::string makeCameraId(const ProvisioningResult& result, const OnvifProvisionedProfile& profile, std::size_t index) {
    const std::string deviceId = makeDeviceId(result);
    const std::string profileId = !profile.profile.token.empty()
        ? sanitizePathPart(profile.profile.token)
        : "profile_" + std::to_string(index);
    return deviceId + "__" + profileId;
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

MediaService::MediaService(CameraManager& cm, OnvifProvisioningRepository& rp, MediaRecordingOptions recordingOptions)
    : cameraManager{cm}, repository{rp}, recordingOptions_{std::move(recordingOptions)} {}

MediaService::~MediaService() {}

void MediaService::addCamera(const std::vector<ProvisioningResult>& results) {
    if (results.empty()) {
        return;
    }

    if (!repository.upsert(results)) {
        LogSync::error("[Provisioning] failed to upsert provisioning results");
    }

    for (const ProvisioningResult& result : results) {
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
            config.camera_id = makeCameraId(result, profile, index);
            config.rtsp_uri = profile.streamUri->uri;
            config.record_pattern = makeRecordPattern(recordingOptions_, config.camera_id);
            config.split_seconds = recordingOptions_.splitSeconds;

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (registeredCameraIds_.find(config.camera_id) != registeredCameraIds_.end()) {
                    continue;
                }
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
    }
}
