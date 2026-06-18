#include "VmsDeviceRepository.h"

#include "DbRowHelpers.h"
#include "DbRuntime.hpp"
#include "LogSync.h"

#include <utility>

namespace {

std::optional<VmsDeviceRepository::DeviceRecord> mapDeviceRecord(const DbTemplate::TypedRow& row) {
    VmsDeviceRepository::DeviceRecord device;
    if (!DbRow::getInt64Cell(row, "device_id", device.deviceId)
        || !DbRow::getInt64Cell(row, "type", device.type)) {
        return std::nullopt;
    }

    DbRow::getStringCell(row, "addr", device.addr);
    DbRow::getStringCell(row, "name", device.name);
    DbRow::getStringCell(row, "model", device.model);
    DbRow::getInt64Cell(row, "online_state", device.onlineState);
    DbRow::getInt64Cell(row, "video_state", device.videoState);
    DbRow::getInt64Cell(row, "channel_count", device.channelCount);
    return device;
}

std::optional<VmsDeviceRepository::ChannelRecord> mapChannelRecord(const DbTemplate::TypedRow& row) {
    VmsDeviceRepository::ChannelRecord channel;
    if (!DbRow::getInt64Cell(row, "channel_id", channel.channelId)
        || !DbRow::getInt64Cell(row, "channel_no", channel.channelNo)) {
        return std::nullopt;
    }

    DbRow::getStringCell(row, "name", channel.name);
    DbRow::getStringCell(row, "video_codec", channel.videoCodec);
    DbRow::getStringCell(row, "audio_codec", channel.audioCodec);
    DbRow::getStringCell(row, "rtsp", channel.rtsp);
    return channel;
}

} // namespace

VmsDeviceRepository::VmsDeviceRepository() = default;

VmsDeviceRepository::~VmsDeviceRepository() = default;

std::vector<VmsDeviceRepository::DeviceRecord> VmsDeviceRepository::listDevices(int limit, int offset) const {
    DbTemplate::ParamMap params;
    params.emplace("limit", static_cast<std::int64_t>(limit));
    params.emplace("offset", static_cast<std::int64_t>(offset));

    std::vector<DeviceRecord> devices;
    const auto rows = AppDb::select("devices.listDevices", params);
    for (const auto& row : rows) {
        auto device = mapDeviceRecord(row);
        if (device) {
            devices.emplace_back(std::move(*device));
        } else {
            LogSync::error("[device.repository] invalid device row");
        }
    }
    return devices;
}

std::optional<VmsDeviceRepository::DeviceRecord> VmsDeviceRepository::findDeviceById(
    std::int64_t deviceId) const {
    DbTemplate::ParamMap params;
    params.emplace("device_id", deviceId);

    const auto rows = AppDb::select("devices.getDeviceById", params);
    if (rows.empty()) {
        return std::nullopt;
    }

    auto device = mapDeviceRecord(rows.front());
    if (!device) {
        LogSync::error("[device.repository] invalid device row for device_id=", deviceId);
    }
    return device;
}

bool VmsDeviceRepository::updateDeviceName(std::int64_t deviceId, const std::string& name) const {
    DbTemplate::ParamMap params;
    params.emplace("device_id", deviceId);
    params.emplace("name", name);
    return AppDb::update("devices.updateDeviceName", params);
}

std::vector<VmsDeviceRepository::ChannelRecord> VmsDeviceRepository::listDeviceChannels(
    std::int64_t deviceId) const {
    DbTemplate::ParamMap params;
    params.emplace("device_id", deviceId);

    std::vector<ChannelRecord> channels;
    const auto rows = AppDb::select("devices.listDeviceChannels", params);
    for (const auto& row : rows) {
        auto channel = mapChannelRecord(row);
        if (channel) {
            channels.emplace_back(std::move(*channel));
        } else {
            LogSync::error("[device.repository] invalid channel row for device_id=", deviceId);
        }
    }
    return channels;
}
