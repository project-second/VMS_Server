#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct PlaybackTargetRecord {
    std::int64_t deviceId = 0;
    std::int64_t channelId = 0;
    std::int64_t profileId = 0;
    std::string deviceName;
    std::string channelName;
};

struct PlaybackSegmentRecord {
    std::int64_t segmentId = 0;
    std::int64_t profileId = 0;
    std::string filePath;
    std::int64_t startMs = 0;
    std::optional<std::int64_t> endMs;
    std::int64_t fileSize = 0;
    std::string codec;
    std::string status;
    bool hasGap = false;
    std::int64_t deviceId = 0;
    std::int64_t channelId = 0;
    std::string deviceName;
    std::string channelName;
};

struct PlaybackSegmentPage {
    std::vector<PlaybackSegmentRecord> items;
    std::int64_t totalCount = 0;
};

class VmsPlaybackRepository {
public:
    std::optional<PlaybackTargetRecord> findTarget(
        std::int64_t deviceId, std::int64_t channelId) const;
    PlaybackSegmentPage findPage(
        std::int64_t profileId, std::int64_t limit, std::int64_t offset) const;
    std::vector<PlaybackSegmentRecord> findRange(
        std::int64_t profileId, std::int64_t fromMs, std::int64_t toMs) const;
    std::optional<PlaybackSegmentRecord> findById(std::int64_t segmentId) const;

    bool saveStarted(
        std::int64_t profileId, const std::string& filePath,
        std::int64_t startMs, const std::string& codec) const;
    bool saveClosed(
        const std::string& filePath, std::int64_t endMs,
        std::int64_t fileSize, const std::string& codec,
        const std::string& status) const;
    bool interruptStaleRecordings() const;
};
