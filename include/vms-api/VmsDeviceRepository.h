#ifndef __VMSDEVICEREPOSITORY_H__
#define __VMSDEVICEREPOSITORY_H__

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class VmsDeviceRepository {
public:
    struct DeviceRecord {
        std::int64_t deviceId = 0;
        std::int64_t type = 0;
        std::string addr;
        std::string name;
        std::string model;
        std::int64_t onlineState = 0;
        std::int64_t videoState = 0;
        std::int64_t channelCount = 0;
    };

    struct ChannelRecord {
        std::int64_t channelId = 0;
        std::int64_t channelNo = 0;
        std::string name;
        std::string videoCodec;
        std::string audioCodec;
        std::string rtsp;
    };

    struct PtzTargetRecord {
        std::int64_t channelId = 0;
        std::int64_t deviceId = 0;
        std::string ptzXAddr;
        std::string profileToken;
        std::string username;
        std::string passwordEnc;
        bool hasCapabilities = false;
        bool supportsPan = true;
        bool supportsTilt = true;
        bool supportsZoom = true;
        bool supportsLinear = true;
    };

    struct PresetRecord {
        std::int64_t presetId = 0;
        std::int64_t channelId = 0;
        std::string presetToken;
        std::string presetName;
        double pan = 0.0;
        double tilt = 0.0;
        double zoom = 0.0;
        double linear = 0.0;
    };

    VmsDeviceRepository();
    ~VmsDeviceRepository();

    std::vector<DeviceRecord> listDevices(int limit, int offset) const;
    std::optional<DeviceRecord> findDeviceById(std::int64_t deviceId) const;
    bool updateDeviceName(std::int64_t deviceId, const std::string& name) const;
    std::vector<ChannelRecord> listDeviceChannels(std::int64_t deviceId) const;
    std::optional<PtzTargetRecord> findPtzTargetByChannelId(std::int64_t channelId) const;
    std::vector<PresetRecord> listChannelPresets(std::int64_t channelId) const;
    std::optional<PresetRecord> findChannelPreset(std::int64_t channelId, std::int64_t presetId) const;
    bool upsertChannelPreset(
        std::int64_t channelId,
        const std::string& presetToken,
        const std::string& presetName) const;
    bool updateChannelPresetName(
        std::int64_t channelId,
        std::int64_t presetId,
        const std::string& presetName) const;
    bool deleteChannelPreset(std::int64_t channelId, std::int64_t presetId) const;
};

#endif // __VMSDEVICEREPOSITORY_H__
