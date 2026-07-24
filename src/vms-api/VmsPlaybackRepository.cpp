#include "VmsPlaybackRepository.h"

#include "DbRowHelpers.h"
#include "DbRuntime.hpp"

namespace {

std::optional<PlaybackSegmentRecord> toSegment(const DbTemplate::TypedRow& row) {
    PlaybackSegmentRecord value;
    if (!DbRow::getInt64Cell(row, "seg_id", value.segmentId) ||
        !DbRow::getInt64Cell(row, "profile_id", value.profileId) ||
        !DbRow::getStringCell(row, "file_path", value.filePath) ||
        !DbRow::getInt64Cell(row, "start_ms", value.startMs)) {
        return std::nullopt;
    }
    std::int64_t endMs = 0;
    if (DbRow::getInt64Cell(row, "end_ms", endMs)) value.endMs = endMs;
    DbRow::getInt64Cell(row, "file_size", value.fileSize);
    DbRow::getStringCell(row, "codec", value.codec);
    DbRow::getStringCell(row, "status", value.status);
    DbRow::getBoolCell(row, "has_gap", value.hasGap);
    DbRow::getInt64Cell(row, "device_id", value.deviceId);
    DbRow::getInt64Cell(row, "channel_id", value.channelId);
    DbRow::getStringCell(row, "device_name", value.deviceName);
    DbRow::getStringCell(row, "channel_name", value.channelName);
    return value;
}

} // namespace

std::optional<PlaybackTargetRecord> VmsPlaybackRepository::findTarget(
    std::int64_t deviceId, std::int64_t channelId) const {
    DbTemplate::ParamMap params{{"device_id", deviceId}, {"channel_id", channelId}};
    const auto rows = AppDb::select("playback.findTarget", params);
    if (rows.empty()) return std::nullopt;
    PlaybackTargetRecord target;
    if (!DbRow::getInt64Cell(rows.front(), "device_id", target.deviceId) ||
        !DbRow::getInt64Cell(rows.front(), "channel_id", target.channelId) ||
        !DbRow::getInt64Cell(rows.front(), "profile_id", target.profileId)) return std::nullopt;
    DbRow::getStringCell(rows.front(), "device_name", target.deviceName);
    DbRow::getStringCell(rows.front(), "channel_name", target.channelName);
    return target;
}

PlaybackSegmentPage VmsPlaybackRepository::findPage(
    std::int64_t profileId, std::int64_t limit, std::int64_t offset) const {
    DbTemplate::ParamMap params{{"profile_id", profileId}, {"limit", limit}, {"offset", offset}};
    PlaybackSegmentPage page;
    for (const auto& row : AppDb::select("playback.findPage", params)) {
        std::int64_t count = 0;
        if (DbRow::getInt64Cell(row, "total_count", count)) page.totalCount = count;
        if (auto item = toSegment(row)) page.items.push_back(std::move(*item));
    }
    if (page.items.empty()) {
        const auto rows = AppDb::select("playback.countByProfile", {{"profile_id", profileId}});
        if (!rows.empty()) DbRow::getInt64Cell(rows.front(), "total_count", page.totalCount);
    }
    return page;
}

std::vector<PlaybackSegmentRecord> VmsPlaybackRepository::findRange(
    std::int64_t profileId, std::int64_t fromMs, std::int64_t toMs) const {
    DbTemplate::ParamMap params{{"profile_id", profileId}, {"from_ms", fromMs}, {"to_ms", toMs}};
    std::vector<PlaybackSegmentRecord> result;
    for (const auto& row : AppDb::select("playback.findRange", params)) {
        if (auto item = toSegment(row)) result.push_back(std::move(*item));
    }
    return result;
}

std::optional<PlaybackSegmentRecord> VmsPlaybackRepository::findById(
    std::int64_t segmentId) const {
    const auto rows = AppDb::select("playback.findById", {{"seg_id", segmentId}});
    if (rows.empty()) return std::nullopt;
    return toSegment(rows.front());
}

bool VmsPlaybackRepository::saveStarted(
    std::int64_t profileId, const std::string& filePath,
    std::int64_t startMs, const std::string& codec) const {
    return AppDb::insert("playback.saveStarted", {
        {"profile_id", profileId}, {"file_path", filePath},
        {"start_ms", startMs}, {"codec", codec}});
}

bool VmsPlaybackRepository::saveClosed(
    const std::string& filePath, std::int64_t endMs,
    std::int64_t fileSize, const std::string& codec,
    const std::string& status) const {
    return AppDb::update("playback.saveClosed", {
        {"file_path", filePath}, {"end_ms", endMs}, {"file_size", fileSize},
        {"codec", codec}, {"status", status}});
}

bool VmsPlaybackRepository::interruptStaleRecordings() const {
    return AppDb::update("playback.interruptStale", {});
}
