#include "VmsDeviceRepository.h"

#include "DbRowHelpers.h"
#include "DbRuntime.hpp"
#include "LogSync.h"

#include <exception>
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

std::optional<VmsDeviceRepository::PtzTargetRecord> mapPtzTargetRecord(const DbTemplate::TypedRow& row) {
    VmsDeviceRepository::PtzTargetRecord target;
    if (!DbRow::getInt64Cell(row, "channel_id", target.channelId)
        || !DbRow::getInt64Cell(row, "device_id", target.deviceId)) {
        return std::nullopt;
    }

    DbRow::getStringCell(row, "ptz_xaddr", target.ptzXAddr);
    DbRow::getStringCell(row, "profile_token", target.profileToken);
    DbRow::getStringCell(row, "username", target.username);
    DbRow::getStringCell(row, "password_enc", target.passwordEnc);

    std::int64_t capabilityChannelId = 0;
    target.hasCapabilities = DbRow::getInt64Cell(row, "capability_channel_id", capabilityChannelId);
    DbRow::getBoolCell(row, "supports_pan", target.supportsPan);
    DbRow::getBoolCell(row, "supports_tilt", target.supportsTilt);
    DbRow::getBoolCell(row, "supports_zoom", target.supportsZoom);
    DbRow::getBoolCell(row, "supports_tour", target.supportsTour);
    DbRow::getBoolCell(row, "supports_linear", target.supportsLinear);
    return target;
}

bool getDoubleCell(const DbTemplate::TypedRow& row, const std::string& key, double& out) {
    const auto it = row.find(key);
    if (it == row.end() || std::holds_alternative<std::monostate>(it->second)) return false;

    if (const auto* value = std::get_if<double>(&it->second)) {
        out = *value;
        return true;
    }
    if (const auto* value = std::get_if<std::int64_t>(&it->second)) {
        out = static_cast<double>(*value);
        return true;
    }
    if (const auto* value = std::get_if<std::string>(&it->second)) {
        try {
            std::size_t consumed = 0;
            const double parsed = std::stod(*value, &consumed);
            if (consumed == value->size()) {
                out = parsed;
                return true;
            }
        } catch (const std::exception&) {
        }
    }
    return false;
}

std::optional<VmsDeviceRepository::PresetRecord> mapPresetRecord(const DbTemplate::TypedRow& row) {
    VmsDeviceRepository::PresetRecord preset;
    if (!DbRow::getInt64Cell(row, "preset_id", preset.presetId)
        || !DbRow::getInt64Cell(row, "channel_id", preset.channelId)) {
        return std::nullopt;
    }

    DbRow::getStringCell(row, "preset_token", preset.presetToken);
    DbRow::getStringCell(row, "preset_name", preset.presetName);
    getDoubleCell(row, "pan", preset.pan);
    getDoubleCell(row, "tilt", preset.tilt);
    getDoubleCell(row, "zoom", preset.zoom);
    getDoubleCell(row, "linear", preset.linear);
    return preset;
}

std::optional<VmsDeviceRepository::TourRecord> mapTourRecord(const DbTemplate::TypedRow& row) {
    VmsDeviceRepository::TourRecord tour;
    if (!DbRow::getInt64Cell(row, "tour_id", tour.tourId)
        || !DbRow::getInt64Cell(row, "channel_id", tour.channelId)) {
        return std::nullopt;
    }

    DbRow::getStringCell(row, "tour_token", tour.tourToken);
    DbRow::getStringCell(row, "tour_name", tour.tourName);
    DbRow::getStringCell(row, "repeat_mode", tour.repeatMode);
    DbRow::getBoolCell(row, "schedule_enabled", tour.scheduleEnabled);
    DbRow::getBoolCell(row, "is_enabled", tour.isEnabled);
    DbRow::getInt64Cell(row, "item_count", tour.itemCount);
    return tour;
}

std::optional<VmsDeviceRepository::TourItemRecord> mapTourItemRecord(const DbTemplate::TypedRow& row) {
    VmsDeviceRepository::TourItemRecord item;
    if (!DbRow::getInt64Cell(row, "tour_item_id", item.tourItemId)
        || !DbRow::getInt64Cell(row, "tour_id", item.tourId)
        || !DbRow::getInt64Cell(row, "preset_id", item.presetId)) {
        return std::nullopt;
    }

    DbRow::getStringCell(row, "preset_token", item.presetToken);
    DbRow::getStringCell(row, "preset_name", item.presetName);
    DbRow::getInt64Cell(row, "seq_no", item.seqNo);
    DbRow::getInt64Cell(row, "stay_seconds", item.staySeconds);
    getDoubleCell(row, "move_speed", item.moveSpeed);
    return item;
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

std::optional<VmsDeviceRepository::PtzTargetRecord> VmsDeviceRepository::findPtzTargetByChannelId(
    std::int64_t channelId) const {
    DbTemplate::ParamMap params;
    params.emplace("channel_id", channelId);

    const auto rows = AppDb::select("devices.getPtzTargetByChannelId", params);
    if (rows.empty()) {
        return std::nullopt;
    }

    auto target = mapPtzTargetRecord(rows.front());
    if (!target) {
        LogSync::error("[device.repository] invalid ptz target row for channel_id=", channelId);
    }
    return target;
}

std::vector<VmsDeviceRepository::PresetRecord> VmsDeviceRepository::listChannelPresets(
    std::int64_t channelId) const {
    DbTemplate::ParamMap params;
    params.emplace("channel_id", channelId);

    std::vector<PresetRecord> presets;
    const auto rows = AppDb::select("devices.listChannelPresets", params);
    for (const auto& row : rows) {
        auto preset = mapPresetRecord(row);
        if (preset) {
            presets.emplace_back(std::move(*preset));
        } else {
            LogSync::error("[device.repository] invalid preset row for channel_id=", channelId);
        }
    }
    return presets;
}

std::optional<VmsDeviceRepository::PresetRecord> VmsDeviceRepository::findChannelPreset(
    std::int64_t channelId,
    std::int64_t presetId) const {
    DbTemplate::ParamMap params;
    params.emplace("channel_id", channelId);
    params.emplace("preset_id", presetId);

    const auto rows = AppDb::select("devices.getChannelPresetById", params);
    if (rows.empty()) {
        return std::nullopt;
    }

    auto preset = mapPresetRecord(rows.front());
    if (!preset) {
        LogSync::error("[device.repository] invalid preset row for preset_id=", presetId);
    }
    return preset;
}

bool VmsDeviceRepository::upsertChannelPreset(
    std::int64_t channelId,
    const std::string& presetToken,
    const std::string& presetName) const {
    DbTemplate::ParamMap params;
    params.emplace("channel_id", channelId);
    params.emplace("preset_token", presetToken);
    params.emplace("preset_name", presetName);
    return AppDb::insert("devices.upsertChannelPreset", params);
}

bool VmsDeviceRepository::updateChannelPresetName(
    std::int64_t channelId,
    std::int64_t presetId,
    const std::string& presetName) const {
    DbTemplate::ParamMap params;
    params.emplace("channel_id", channelId);
    params.emplace("preset_id", presetId);
    params.emplace("preset_name", presetName);
    return AppDb::update("devices.updateChannelPresetName", params);
}

bool VmsDeviceRepository::deleteChannelPreset(std::int64_t channelId, std::int64_t presetId) const {
    DbTemplate::ParamMap params;
    params.emplace("channel_id", channelId);
    params.emplace("preset_id", presetId);
    return AppDb::remove("devices.deleteChannelPreset", params);
}


std::vector<VmsDeviceRepository::TourRecord> VmsDeviceRepository::listChannelTours(
    std::int64_t channelId) const {
    DbTemplate::ParamMap params;
    params.emplace("channel_id", channelId);

    std::vector<TourRecord> tours;
    const auto rows = AppDb::select("devices.listChannelTours", params);
    for (const auto& row : rows) {
        auto tour = mapTourRecord(row);
        if (tour) {
            tours.emplace_back(std::move(*tour));
        } else {
            LogSync::error("[device.repository] invalid tour row for channel_id=", channelId);
        }
    }
    return tours;
}

std::optional<VmsDeviceRepository::TourRecord> VmsDeviceRepository::findChannelTour(
    std::int64_t channelId,
    std::int64_t tourId) const {
    DbTemplate::ParamMap params;
    params.emplace("channel_id", channelId);
    params.emplace("tour_id", tourId);

    const auto rows = AppDb::select("devices.getChannelTourById", params);
    if (rows.empty()) {
        return std::nullopt;
    }

    auto tour = mapTourRecord(rows.front());
    if (!tour) {
        LogSync::error("[device.repository] invalid tour row for tour_id=", tourId);
    }
    return tour;
}

std::optional<VmsDeviceRepository::TourRecord> VmsDeviceRepository::findChannelTourByToken(
    std::int64_t channelId,
    const std::string& tourToken) const {
    DbTemplate::ParamMap params;
    params.emplace("channel_id", channelId);
    params.emplace("tour_token", tourToken);

    const auto rows = AppDb::select("devices.getChannelTourByToken", params);
    if (rows.empty()) {
        return std::nullopt;
    }

    auto tour = mapTourRecord(rows.front());
    if (!tour) {
        LogSync::error("[device.repository] invalid tour row for tour_token=", tourToken);
    }
    return tour;
}

std::optional<VmsDeviceRepository::TourRecord> VmsDeviceRepository::upsertChannelTour(
    std::int64_t channelId,
    const std::string& tourToken,
    const std::string& tourName,
    const std::string& repeatMode,
    bool scheduleEnabled,
    bool isEnabled) const {
    DbTemplate::ParamMap params;
    params.emplace("channel_id", channelId);
    params.emplace("tour_token", tourToken);
    params.emplace("tour_name", tourName);
    params.emplace("repeat_mode", repeatMode);
    params.emplace("schedule_enabled", scheduleEnabled);
    params.emplace("is_enabled", isEnabled);

    if (!AppDb::insert("devices.upsertChannelTour", params)) {
        return std::nullopt;
    }
    return findChannelTourByToken(channelId, tourToken);
}

std::vector<VmsDeviceRepository::TourItemRecord> VmsDeviceRepository::listChannelTourItems(
    std::int64_t tourId) const {
    DbTemplate::ParamMap params;
    params.emplace("tour_id", tourId);

    std::vector<TourItemRecord> items;
    const auto rows = AppDb::select("devices.listChannelTourItems", params);
    for (const auto& row : rows) {
        auto item = mapTourItemRecord(row);
        if (item) {
            items.emplace_back(std::move(*item));
        } else {
            LogSync::error("[device.repository] invalid tour item row for tour_id=", tourId);
        }
    }
    return items;
}

bool VmsDeviceRepository::replaceChannelTourItems(
    std::int64_t tourId,
    const std::vector<TourItemRecord>& items) const {
    DbTemplate::ParamMap deleteParams;
    deleteParams.emplace("tour_id", tourId);
    if (!AppDb::remove("devices.deleteChannelTourItems", deleteParams)) {
        return false;
    }

    for (const TourItemRecord& item : items) {
        DbTemplate::ParamMap params;
        params.emplace("tour_id", tourId);
        params.emplace("preset_id", item.presetId);
        params.emplace("seq_no", item.seqNo);
        params.emplace("stay_seconds", item.staySeconds);
        params.emplace("move_speed", item.moveSpeed);
        if (!AppDb::insert("devices.insertChannelTourItem", params)) {
            return false;
        }
    }
    return true;
}

bool VmsDeviceRepository::deleteChannelTour(std::int64_t channelId, std::int64_t tourId) const {
    DbTemplate::ParamMap params;
    params.emplace("channel_id", channelId);
    params.emplace("tour_id", tourId);
    return AppDb::remove("devices.deleteChannelTour", params);
}
