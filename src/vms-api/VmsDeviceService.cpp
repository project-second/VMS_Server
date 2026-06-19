#include "VmsDeviceService.h"

#include "LogSync.h"
#include "OnvifPtzClient.h"
#include "StringHelpers.h"
#include "VmsDeviceRepository.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

VmsDeviceRepository& defaultDeviceRepository() {
    static VmsDeviceRepository repository;
    return repository;
}

OnvifPtzClient& defaultPtzClient() {
    static OnvifPtzClient client;
    return client;
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

VmsDeviceService::VmsDeviceService()
    : deviceRepository(defaultDeviceRepository()), ptzClient(defaultPtzClient()) {}

VmsDeviceService::VmsDeviceService(VmsDeviceRepository& deviceRepository)
    : deviceRepository(deviceRepository), ptzClient(defaultPtzClient()) {}

VmsDeviceService::VmsDeviceService(VmsDeviceRepository& deviceRepository, OnvifPtzClient& ptzClient)
    : deviceRepository(deviceRepository), ptzClient(ptzClient) {}

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


namespace {

bool validVelocity(float value) {
    return std::isfinite(value) && value >= -1.0f && value <= 1.0f;
}

bool isConfigured(const VmsDeviceRepository::PtzTargetRecord& target) {
    return !trim(target.ptzXAddr).empty() && !trim(target.profileToken).empty();
}

} // namespace

VmsDeviceService::PtzControlResult VmsDeviceService::panChannel(std::int64_t channelId, float speed) {
    if (!validVelocity(speed) || speed == 0.0f) {
        return {PtzControlStatus::InvalidArgument};
    }

    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        return {PtzControlStatus::NotFound};
    }
    if (!isConfigured(*target) || (target->hasCapabilities && !target->supportsPan)) {
        return {PtzControlStatus::Unsupported};
    }

    if (!ptzClient.continuousMove(target->ptzXAddr, target->profileToken, speed, 0.0f, 0.0f)) {
        LogSync::error("[device.ptz.pan] ONVIF ContinuousMove failed for channel_id=", channelId);
        return {PtzControlStatus::UpstreamError};
    }
    return {PtzControlStatus::Success};
}

VmsDeviceService::PtzControlResult VmsDeviceService::tiltChannel(std::int64_t channelId, float speed) {
    if (!validVelocity(speed) || speed == 0.0f) {
        return {PtzControlStatus::InvalidArgument};
    }

    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        return {PtzControlStatus::NotFound};
    }
    if (!isConfigured(*target) || (target->hasCapabilities && !target->supportsTilt)) {
        return {PtzControlStatus::Unsupported};
    }

    if (!ptzClient.continuousMove(target->ptzXAddr, target->profileToken, 0.0f, speed, 0.0f)) {
        LogSync::error("[device.ptz.tilt] ONVIF ContinuousMove failed for channel_id=", channelId);
        return {PtzControlStatus::UpstreamError};
    }
    return {PtzControlStatus::Success};
}

VmsDeviceService::PtzControlResult VmsDeviceService::moveChannelLinear(
    std::int64_t channelId,
    float pan,
    float tilt,
    float zoom) {
    if (!validVelocity(pan) || !validVelocity(tilt) || !validVelocity(zoom)
        || (pan == 0.0f && tilt == 0.0f && zoom == 0.0f)) {
        return {PtzControlStatus::InvalidArgument};
    }

    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        return {PtzControlStatus::NotFound};
    }
    if (!isConfigured(*target)
        || (target->hasCapabilities
            && (!target->supportsLinear
                || (pan != 0.0f && !target->supportsPan)
                || (tilt != 0.0f && !target->supportsTilt)
                || (zoom != 0.0f && !target->supportsZoom)))) {
        return {PtzControlStatus::Unsupported};
    }

    if (!ptzClient.continuousMove(target->ptzXAddr, target->profileToken, pan, tilt, zoom)) {
        LogSync::error("[device.ptz.linear] ONVIF ContinuousMove failed for channel_id=", channelId);
        return {PtzControlStatus::UpstreamError};
    }
    return {PtzControlStatus::Success};
}

VmsDeviceService::PtzControlResult VmsDeviceService::stopChannel(
    std::int64_t channelId,
    bool panTilt,
    bool zoom) {
    if (!panTilt && !zoom) {
        return {PtzControlStatus::InvalidArgument};
    }

    const auto target = deviceRepository.findPtzTargetByChannelId(channelId);
    if (!target) {
        return {PtzControlStatus::NotFound};
    }
    if (!isConfigured(*target)) {
        return {PtzControlStatus::Unsupported};
    }

    if (!ptzClient.stop(target->ptzXAddr, target->profileToken, panTilt, zoom)) {
        LogSync::error("[device.ptz.stop] ONVIF Stop failed for channel_id=", channelId);
        return {PtzControlStatus::UpstreamError};
    }
    return {PtzControlStatus::Success};
}
