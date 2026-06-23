#ifndef __VMSDEVICESERVICE_H__
#define __VMSDEVICESERVICE_H__

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class OnvifPtzClient;
class VmsDeviceRepository;

class VmsDeviceService {
public:
    enum class ListStatus {
        Success,
        NotFound,
        Unsupported,
        InternalError
    };

    enum class MutationStatus {
        Success,
        NotFound,
        InvalidArgument,
        InternalError
    };

    enum class PtzControlStatus {
        Success,
        NotFound,
        InvalidArgument,
        Unsupported,
        Busy,
        UpstreamError,
        InternalError
    };

    struct DeviceItem {
        std::int64_t deviceId = 0;
        std::int64_t type = 0;
        std::string ip;
        std::string name;
        std::string model;
        bool online = false;
        std::string health;
        std::int64_t channelCount = 0;
    };

    struct ChannelItem {
        std::int64_t channelId = 0;
        std::int64_t channelNo = 0;
        std::string name;
        std::string videoCodec;
        std::string audioCodec;
        std::string rtsp;
    };

    struct PresetItem {
        std::int64_t presetId = 0;
        std::string presetToken;
        std::string presetName;
        double pan = 0.0;
        double tilt = 0.0;
        double zoom = 0.0;
        double linear = 0.0;
    };

    struct TourItem {
        std::int64_t tourItemId = 0;
        std::int64_t presetId = 0;
        std::string presetToken;
        std::string presetName;
        std::int64_t seqNo = 0;
        std::int64_t staySeconds = 0;
        double moveSpeed = 0.0;
    };

    struct TourItemInput {
        std::int64_t presetId = 0;
        std::int64_t seqNo = 0;
        std::int64_t staySeconds = 0;
        double moveSpeed = 0.0;
    };

    struct TourWriteRequest {
        std::string tourName;
        std::string repeatMode = "LOOP";
        bool isEnabled = true;
        std::vector<TourItemInput> items;
    };

    struct TourItemResult {
        std::int64_t tourId = 0;
        std::string tourToken;
        std::string tourName;
        std::string repeatMode;
        bool isEnabled = false;
        std::int64_t itemCount = 0;
        std::vector<TourItem> items;
    };

    struct DeviceListResult {
        ListStatus status = ListStatus::InternalError;
        int page = 1;
        int size = 10;
        std::vector<DeviceItem> items;
    };

    struct ChannelListResult {
        ListStatus status = ListStatus::InternalError;
        std::int64_t deviceId = 0;
        std::vector<ChannelItem> items;
    };

    struct PresetListResult {
        ListStatus status = ListStatus::InternalError;
        std::int64_t channelId = 0;
        std::vector<PresetItem> items;
    };

    struct TourListResult {
        ListStatus status = ListStatus::InternalError;
        std::int64_t channelId = 0;
        std::vector<TourItemResult> items;
    };

    struct TourDetailResult {
        ListStatus status = ListStatus::InternalError;
        std::int64_t channelId = 0;
        TourItemResult item;
    };

    struct TourMutationResult {
        PtzControlStatus status = PtzControlStatus::InternalError;
        std::optional<TourItemResult> item;
    };

    struct ChangeNameResult {
        MutationStatus status = MutationStatus::InternalError;
        std::int64_t deviceId = 0;
        std::string name;
    };

    struct PtzControlResult {
        PtzControlStatus status = PtzControlStatus::InternalError;
    };

public:
    VmsDeviceService();
    explicit VmsDeviceService(VmsDeviceRepository& deviceRepository);
    VmsDeviceService(VmsDeviceRepository& deviceRepository, OnvifPtzClient& ptzClient);
    ~VmsDeviceService();

    DeviceListResult getDeviceList(int page, int size);
    ChangeNameResult changeDeviceName(std::int64_t deviceId, const std::string& name);
    ChannelListResult getDeviceChannelList(std::int64_t deviceId);
    PtzControlResult panChannel(std::int64_t channelId, float speed);
    PtzControlResult tiltChannel(std::int64_t channelId, float speed);
    PtzControlResult moveChannelLinear(std::int64_t channelId, float pan, float tilt, float zoom);
    PtzControlResult stopChannel(std::int64_t channelId, bool panTilt, bool zoom);
    PresetListResult getChannelPresetList(std::int64_t channelId);
    PtzControlResult setChannelPreset(std::int64_t channelId, const std::string& presetName);
    PtzControlResult goToChannelPreset(std::int64_t channelId, std::int64_t presetId);
    PtzControlResult updateChannelPreset(
        std::int64_t channelId,
        std::int64_t presetId,
        const std::string& presetName);
    PtzControlResult deleteChannelPreset(std::int64_t channelId, std::int64_t presetId);

    TourListResult getChannelTourList(std::int64_t channelId);
    TourDetailResult getChannelTourInfo(std::int64_t channelId, std::int64_t tourId);
    TourMutationResult createChannelTour(std::int64_t channelId, const TourWriteRequest& request);
    TourMutationResult updateChannelTour(
        std::int64_t channelId,
        std::int64_t tourId,
        const TourWriteRequest& request);
    PtzControlResult startChannelTour(std::int64_t channelId, std::int64_t tourId);
    PtzControlResult stopChannelTour(std::int64_t channelId, std::int64_t tourId);
    PtzControlResult deleteChannelTour(std::int64_t channelId, std::int64_t tourId);

private:
    VmsDeviceRepository& deviceRepository;
    OnvifPtzClient& ptzClient;
};

#endif // __VMSDEVICESERVICE_H__
