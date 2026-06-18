#ifndef ONVIF_PTZ_CLIENT_H
#define ONVIF_PTZ_CLIENT_H

#include "OnvifClientOptions.h"

#include <optional>
#include <string>
#include <vector>

using OnvifPtzClientOptions = OnvifClientOptions;

struct OnvifPtzPreset {
    std::string token;
    std::string name;
};

class OnvifPtzClient {
public:
    explicit OnvifPtzClient(OnvifPtzClientOptions options = {});

    std::optional<std::vector<OnvifPtzPreset>> getPresets(
        const std::string& ptzServiceUrl,
        const std::string& profileToken) const;

    std::optional<std::string> setPreset(
        const std::string& ptzServiceUrl,
        const std::string& profileToken,
        const std::string& presetName,
        const std::string& presetToken = {}) const;

    bool gotoPreset(
        const std::string& ptzServiceUrl,
        const std::string& profileToken,
        const std::string& presetToken) const;

    bool removePreset(
        const std::string& ptzServiceUrl,
        const std::string& profileToken,
        const std::string& presetToken) const;

    bool continuousMove(
        const std::string& ptzServiceUrl,
        const std::string& profileToken,
        float pan,
        float tilt,
        float zoom = 0.0f) const;

    bool stop(
        const std::string& ptzServiceUrl,
        const std::string& profileToken,
        bool panTilt = true,
        bool zoom = true) const;

private:
    OnvifPtzClientOptions options_;
};

#endif // ONVIF_PTZ_CLIENT_H
