#include "VmsPlaybackService.h"

#include "LogSync.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace {

std::chrono::system_clock::time_point fromMs(std::int64_t value) {
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(value));
}

std::int64_t toMs(const std::chrono::system_clock::time_point& value) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        value.time_since_epoch()).count();
}

std::vector<RecordingSegment> toRecordingSegments(
    const std::vector<PlaybackSegmentRecord>& rows,
    const std::string& cameraId) {
    std::vector<RecordingSegment> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        if (!row.endMs) continue;
        RecordingSegment segment;
        segment.profile_id = row.profileId;
        segment.camera_id = cameraId;
        segment.file_path = row.filePath;
        segment.started_at = fromMs(row.startMs);
        segment.ended_at = fromMs(*row.endMs);
        segment.status = row.status;
        result.push_back(std::move(segment));
    }
    return result;
}

} // namespace

VmsPlaybackService::VmsPlaybackService(
    VmsPlaybackRepository& repository,
    std::filesystem::path cacheRoot)
    : repository_(repository), sessionManager_(std::move(cacheRoot)) {}

std::optional<PlaybackTargetRecord> VmsPlaybackService::findTarget(
    std::int64_t deviceId, std::int64_t channelId) const {
    return repository_.findTarget(deviceId, channelId);
}

PlaybackSegmentPage VmsPlaybackService::findPage(
    std::int64_t profileId, std::int64_t page, std::int64_t size) const {
    return repository_.findPage(profileId, size, (page - 1) * size);
}

std::vector<PlaybackSegmentRecord> VmsPlaybackService::findRange(
    std::int64_t profileId, std::int64_t fromMsValue, std::int64_t toMsValue) const {
    return repository_.findRange(profileId, fromMsValue, toMsValue);
}

std::optional<PlaybackSegmentRecord> VmsPlaybackService::findSegment(
    std::int64_t segmentId) const {
    return repository_.findById(segmentId);
}

bool VmsPlaybackService::persistRecordingSegment(const RecordingSegment& segment) const {
    const auto startedMs = toMs(segment.started_at);
    if (segment.status == "recording") {
        return repository_.saveStarted(
            segment.profile_id, segment.file_path, startedMs, "H264");
    }
    if (!segment.ended_at) return false;
    std::error_code error;
    const auto size = std::filesystem::file_size(segment.file_path, error);
    return repository_.saveClosed(
        segment.file_path, toMs(*segment.ended_at),
        error ? 0 : static_cast<std::int64_t>(size), "H264", segment.status);
}

bool VmsPlaybackService::interruptStaleRecordings() const {
    return repository_.interruptStaleRecordings();
}

PlaybackHlsResult VmsPlaybackService::createHls(
    const PlaybackTargetRecord& target,
    std::int64_t fromMsValue, std::int64_t toMsValue) {
    PlaybackHlsResult result;
    if (fromMsValue >= toMsValue || toMsValue - fromMsValue > 60LL * 60LL * 1000LL) {
        result.errorMessage = "playback range must be greater than 0 and at most 1 hour";
        return result;
    }

    const std::string cameraId = "profile_" + std::to_string(target.profileId);
    PlaybackRequest request{cameraId, fromMs(fromMsValue), fromMs(toMsValue)};
    const auto rows = repository_.findRange(target.profileId, fromMsValue, toMsValue);
    auto playback = playbackService_.create_session(
        request, toRecordingSegments(rows, cameraId));
    if (!playback.ok || !playback.session) {
        result.notFound = true;
        result.errorMessage = playback.error_message;
        return result;
    }

    auto hls = sessionManager_.create_session(*playback.session);
    if (!hls.ok || !hls.session) {
        result.errorMessage = hls.error_message;
        return result;
    }
    auto converted = converter_.convert_all_segments(*hls.session);
    if (!converted.ok) {
        result.errorMessage = converted.error_message;
        return result;
    }
    auto playlist = playlistBuilder_.write_playlist(*hls.session);
    if (!playlist.ok) {
        result.errorMessage = playlist.error_message;
        return result;
    }

    std::ifstream input(hls.session->playlist_path);
    if (!input.is_open()) {
        result.errorMessage = "failed to read generated HLS playlist";
        return result;
    }
    std::ostringstream rewritten;
    std::string line;
    while (std::getline(input, line)) {
        const auto found = std::find_if(
            hls.session->segments.begin(), hls.session->segments.end(),
            [&line](const HlsVodSegment& segment) {
                return segment.output_file_name == line;
            });
        if (found != hls.session->segments.end()) {
            rewritten << "/playback/sessions/" << hls.session->session_id
                      << "/segments/" << found->output_file_name << "\n";
        } else {
            rewritten << line << "\n";
        }
    }

    cleanupExpiredSessions();
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessions_[hls.session->session_id] = *hls.session;
    }
    result.ok = true;
    result.sessionId = hls.session->session_id;
    result.playlist = rewritten.str();
    return result;
}

PlaybackBinaryResult VmsPlaybackService::readHlsSegment(
    const std::string& sessionId, const std::string& fileName) {
    cleanupExpiredSessions();
    std::filesystem::path path;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        const auto it = sessions_.find(sessionId);
        if (it == sessions_.end()) return {false, "HLS session not found", {}};
        const auto segment = std::find_if(
            it->second.segments.begin(), it->second.segments.end(),
            [&fileName](const HlsVodSegment& item) {
                return item.output_file_name == fileName;
            });
        if (segment == it->second.segments.end()) {
            return {false, "HLS segment not found", {}};
        }
        path = segment->output_file_path;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) return {false, "HLS segment file not found", {}};
    std::ostringstream body;
    body << input.rdbuf();
    return {true, {}, body.str()};
}

void VmsPlaybackService::cleanupExpiredSessions() {
    const auto threshold = std::chrono::system_clock::now() - sessionTtl_;
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second.created_at < threshold) {
            std::error_code error;
            std::filesystem::remove_all(it->second.cache_dir, error);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}
