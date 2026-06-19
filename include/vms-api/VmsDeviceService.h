#ifndef __VMSDEVICESERVICE_H__
#define __VMSDEVICESERVICE_H__

#include <cstdint>
#include <string>
#include <vector>

class OnvifPtzClient;
class VmsDeviceRepository;

class VmsDeviceService {
public:
    enum class ListStatus {
        Success,
        NotFound,
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

private:
    VmsDeviceRepository& deviceRepository;
    OnvifPtzClient& ptzClient;
};

#endif // __VMSDEVICESERVICE_H__
