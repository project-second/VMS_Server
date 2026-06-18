#ifndef __VMSDEVICESERVICE_H__
#define __VMSDEVICESERVICE_H__

#include <cstdint>
#include <string>
#include <vector>

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

public:
    VmsDeviceService();
    explicit VmsDeviceService(VmsDeviceRepository& deviceRepository);
    ~VmsDeviceService();

    DeviceListResult getDeviceList(int page, int size);
    ChangeNameResult changeDeviceName(std::int64_t deviceId, const std::string& name);
    ChannelListResult getDeviceChannelList(std::int64_t deviceId);

private:
    VmsDeviceRepository& deviceRepository;
};

#endif // __VMSDEVICESERVICE_H__
