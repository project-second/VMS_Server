#pragma once

#include "PlaybackService.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// HLS VOD session 안에서 생성될 예정인 media segment 하나를 표현한다.
// 33강에서는 실제 .ts 파일을 만들지 않고, 어떤 파일명이 만들어질지만 계획한다.
struct HlsVodSegment {
    std::size_t sequence_no = 0;

    std::string source_file_path;
    std::int64_t source_offset_ms = 0;
    std::int64_t duration_ms = 0;

    std::chrono::system_clock::time_point program_date_time;

    std::string output_file_name;
    std::filesystem::path output_file_path;
};

// PlaybackSession을 HLS VOD로 제공하기 위한 cache session 정보다.
// 33강에서는 디렉터리와 metadata만 만든다.
struct HlsVodSession {
    std::string session_id;
    std::string camera_id;

    std::chrono::system_clock::time_point requested_from;
    std::chrono::system_clock::time_point requested_to;
    std::chrono::system_clock::time_point created_at;

    std::filesystem::path cache_dir;
    std::string playlist_file_name = "index.m3u8";
    std::filesystem::path playlist_path;

    std::vector<HlsVodSegment> segments;
    std::int64_t total_duration_ms = 0;
};

struct HlsVodSessionCreateResult {
    bool ok = false;
    std::string error_message;
    std::optional<HlsVodSession> session;
};

// HLS VOD session cache 디렉터리와 metadata를 만드는 관리자다.
//
// 33강 범위:
// - playback_cache/session_xxx/ 디렉터리 생성
// - index.m3u8가 놓일 경로 계산
// - seg_000.ts, seg_001.ts 같은 출력 segment 계획 생성
// - 디버그용 session_info.txt 작성
//
// 아직 하지 않는 것:
// - MP4 -> TS 변환
// - index.m3u8 작성
// - HTTP 제공
class HlsVodSessionManager {
public:
    explicit HlsVodSessionManager(
        std::filesystem::path cache_root = "playback_cache"
    );

    HlsVodSessionCreateResult create_session(
        const PlaybackSession& playback_session
    ) const;

    const std::filesystem::path& cache_root() const;

    static void print_create_result(
        const HlsVodSessionCreateResult& result
    );

private:
    HlsVodSessionCreateResult validate_playback_session(
        const PlaybackSession& playback_session
    ) const;

    HlsVodSegment make_segment(
        const PlaybackEntry& entry,
        std::size_t sequence_no,
        const std::filesystem::path& cache_dir
    ) const;

    bool write_debug_manifest(
        const HlsVodSession& session,
        std::string& error_message
    ) const;

    static std::string make_segment_file_name(std::size_t sequence_no);

    static std::string time_to_string(
        const std::chrono::system_clock::time_point& time
    );

private:
    std::filesystem::path cache_root_;
};
