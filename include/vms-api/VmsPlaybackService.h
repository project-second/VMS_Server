#pragma once

#include "HlsVodConverter.h"
#include "HlsVodPlaylistBuilder.h"
#include "HlsVodSession.h"
#include "RecordingSegmentStore.h"
#include "VmsPlaybackRepository.h"

#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct PlaybackHlsResult {
    bool ok = false;
    bool notFound = false;
    std::string errorMessage;
    std::string sessionId;
    std::string playlist;
};

struct PlaybackBinaryResult {
    bool ok = false;
    std::string errorMessage;
    std::string body;
};

class VmsPlaybackService {
public:
    VmsPlaybackService(
        VmsPlaybackRepository& repository,
        std::filesystem::path cacheRoot);

    std::optional<PlaybackTargetRecord> findTarget(
        std::int64_t deviceId, std::int64_t channelId) const;
    PlaybackSegmentPage findPage(
        std::int64_t profileId, std::int64_t page, std::int64_t size) const;
    std::vector<PlaybackSegmentRecord> findRange(
        std::int64_t profileId, std::int64_t fromMs, std::int64_t toMs) const;
    std::optional<PlaybackSegmentRecord> findSegment(std::int64_t segmentId) const;

    bool persistRecordingSegment(const RecordingSegment& segment) const;
    bool interruptStaleRecordings() const;

    PlaybackHlsResult createHls(
        const PlaybackTargetRecord& target,
        std::int64_t fromMs, std::int64_t toMs);
    PlaybackBinaryResult readHlsSegment(
        const std::string& sessionId, const std::string& fileName);

private:
    void cleanupExpiredSessions();

    VmsPlaybackRepository& repository_;
    PlaybackService playbackService_;
    HlsVodSessionManager sessionManager_;
    HlsVodConverter converter_;
    HlsVodPlaylistBuilder playlistBuilder_;
    std::mutex sessionsMutex_;
    std::unordered_map<std::string, HlsVodSession> sessions_;
    std::chrono::minutes sessionTtl_{15};
};
