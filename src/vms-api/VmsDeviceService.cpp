#include "VmsDeviceService.h"

#include "LogSync.h"
#include "StringHelpers.h"
#include "VmsDeviceRepository.h"

#include <algorithm>
#include <utility>

namespace {

VmsDeviceRepository& defaultDeviceRepository() {
    static VmsDeviceRepository repository;
    return repository;
}

std::string healthName(std::int64_t onlineState, std::int64_t videoState) {
    if (onlineState <= 0) {
        return "DOWN";
    }
    if (videoState <= 0) {
        return "WARN";
    }
    return "OK";
}

VmsDeviceService::DeviceItem toDeviceItem(const VmsDeviceRepository::DeviceRecord& record) {
    VmsDeviceService::DeviceItem item;
    item.deviceId = record.deviceId;
    item.type = record.type;
    item.ip = record.addr;
    item.name = record.name;
    item.model = record.model;
    item.online = record.onlineState > 0;
    item.health = healthName(record.onlineState, record.videoState);
    item.channelCount = record.channelCount;
    return item;
}

VmsDeviceService::ChannelItem toChannelItem(const VmsDeviceRepository::ChannelRecord& record) {
    VmsDeviceService::ChannelItem item;
    item.channelId = record.channelId;
    item.channelNo = record.channelNo;
    item.name = record.name;
    item.videoCodec = record.videoCodec;
    item.audioCodec = record.audioCodec;
    item.rtsp = record.rtsp;
    return item;
}

} // namespace

VmsDeviceService::VmsDeviceService() : deviceRepository(defaultDeviceRepository()) {}

VmsDeviceService::VmsDeviceService(VmsDeviceRepository& deviceRepository)
    : deviceRepository(deviceRepository) {}

VmsDeviceService::~VmsDeviceService() {}

VmsDeviceService::DeviceListResult VmsDeviceService::getDeviceList(int page, int size) {
    const int normalizedPage = std::max(1, page);
    const int normalizedSize = std::max(1, std::min(size, 100));

    DeviceListResult result;
    result.page = normalizedPage;
    result.size = normalizedSize;

    const auto devices = deviceRepository.listDevices(
        normalizedSize,
        (normalizedPage - 1) * normalizedSize);
    for (const auto& device : devices) {
        result.items.emplace_back(toDeviceItem(device));
    }

    result.status = ListStatus::Success;
    return result;
}

VmsDeviceService::ChangeNameResult VmsDeviceService::changeDeviceName(
    std::int64_t deviceId,
    const std::string& name) {
    ChangeNameResult result;
    result.deviceId = deviceId;
    result.name = trim(name);

    if (result.name.empty() || result.name.size() > 128) {
        result.status = MutationStatus::InvalidArgument;
        return result;
    }

    if (!deviceRepository.findDeviceById(deviceId)) {
        result.status = MutationStatus::NotFound;
        return result;
    }

    if (!deviceRepository.updateDeviceName(deviceId, result.name)) {
        LogSync::error("[device.changeName] update failed for device_id=", deviceId);
        result.status = MutationStatus::InternalError;
        return result;
    }

    result.status = MutationStatus::Success;
    return result;
}

VmsDeviceService::ChannelListResult VmsDeviceService::getDeviceChannelList(std::int64_t deviceId) {
    ChannelListResult result;
    result.deviceId = deviceId;

    if (!deviceRepository.findDeviceById(deviceId)) {
        result.status = ListStatus::NotFound;
        return result;
    }

    const auto channels = deviceRepository.listDeviceChannels(deviceId);
    for (const auto& channel : channels) {
        result.items.emplace_back(toChannelItem(channel));
    }

    result.status = ListStatus::Success;
    return result;
}
