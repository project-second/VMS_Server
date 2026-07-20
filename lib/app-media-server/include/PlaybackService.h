#pragma once

#include "RecordingSegmentStore.h"

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

struct PlaybackRequest {
    std::string camera_id;
    std::chrono::system_clock::time_point from;
    std::chrono::system_clock::time_point to;
};

struct PlaybackEntry {
    std::string file_path;

    std::chrono::system_clock::time_point segment_started_at;
    std::chrono::system_clock::time_point segment_ended_at;

    std::chrono::system_clock::time_point play_from;
    std::chrono::system_clock::time_point play_to;

    std::int64_t play_from_offset_ms = 0;
    std::int64_t play_duration_ms = 0;

    std::string source_status;
};

struct PlaybackSession {
    std::string session_id;
    std::string camera_id;

    std::chrono::system_clock::time_point requested_from;
    std::chrono::system_clock::time_point requested_to;
    std::chrono::system_clock::time_point created_at;

    std::vector<PlaybackEntry> entries;
    std::int64_t total_duration_ms = 0;
};

struct PlaybackResponse {
    bool ok = false;
    std::string error_message;
    std::optional<PlaybackSession> session;
};

// 사용자가 playback session 안에서 특정 시각으로 이동하려는 요청이다.
// seek_time은 CCTV 절대 시각 기준이다.
struct PlaybackSeekRequest {
    std::string session_id;
    std::chrono::system_clock::time_point seek_time;
};

struct PlaybackSeekResult {
    bool ok = false;
    std::string error_message;

    std::string session_id;
    std::string camera_id;
    std::chrono::system_clock::time_point seek_time;

    // seek_time이 포함된 PlaybackEntry의 위치다.
    std::size_t entry_index = 0;

    // 원본 MP4 파일에서 어느 지점부터 읽어야 하는지 나타낸다.
    std::string file_path;
    std::int64_t file_offset_ms = 0;

    // 사용자가 요청한 전체 절대시간 범위에서 seek_time이 얼마나 떨어져 있는지.
    // CCTV 타임라인 UI에서 쓰기 좋다.
    std::int64_t absolute_timeline_offset_ms = 0;

    // PlaybackSession의 실제 재생 흐름 기준 offset이다.
    // 녹화 공백이 있으면 absolute_timeline_offset_ms와 달라질 수 있다.
    std::int64_t session_playback_offset_ms = 0;

    // seek 이후 다시 HLS VOD playlist를 만든다면 사용할 수 있는 entry 목록이다.
    std::vector<PlaybackEntry> remaining_entries;
    std::int64_t remaining_duration_ms = 0;
};

// 사용자의 과거 영상 재생 요청을 녹화 segment 목록으로 바꾸는 서비스다.
//
// 31강 범위:
// - PlaybackRequest를 PlaybackSession으로 변환했다.
//
// 32강 범위:
// - PlaybackSession 안에서 seek_time이 어느 파일의 몇 ms offset인지 찾는다.
// - HLS player용 session playback offset과 CCTV 절대시간 offset을 구분한다.
class PlaybackService {
public:
    explicit PlaybackService(const RecordingSegmentStore& segment_store);

    PlaybackResponse create_session(const PlaybackRequest& request) const;

    PlaybackSeekResult seek(
        const PlaybackSession& session,
        const PlaybackSeekRequest& request
    ) const;

    static void print_response(const PlaybackResponse& response);
    static void print_seek_result(const PlaybackSeekResult& result);

private:
    PlaybackResponse validate_request(const PlaybackRequest& request) const;

    static std::string make_session_id();

    static std::optional<PlaybackEntry> make_entry_from_segment(
        const RecordingSegment& segment,
        const PlaybackRequest& request
    );

    static std::vector<PlaybackEntry> make_remaining_entries_from_seek(
        const PlaybackSession& session,
        std::size_t entry_index,
        const std::chrono::system_clock::time_point& seek_time,
        std::int64_t file_offset_ms
    );

    static std::int64_t total_duration_ms(
        const std::vector<PlaybackEntry>& entries
    );

    static std::string time_to_string(
        const std::chrono::system_clock::time_point& time
    );

private:
    const RecordingSegmentStore& segment_store_;
};
