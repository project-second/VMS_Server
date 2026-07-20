#pragma once

#include "HlsVodSession.h"

#include <filesystem>
#include <string>

struct HlsVodPlaylistWriteResult {
    bool ok = false;
    std::string error_message;
    std::filesystem::path playlist_path;
    std::size_t segment_count = 0;
    std::int64_t target_duration_seconds = 0;
};

// HlsVodSession에 들어있는 segment 계획을 바탕으로 index.m3u8 파일을 작성한다.
//
// 34강 범위:
// - index.m3u8 생성
// - #EXTM3U, #EXT-X-PLAYLIST-TYPE:VOD, #EXTINF 작성
// - #EXT-X-PROGRAM-DATE-TIME으로 CCTV 절대시간 매핑
//
// 아직 하지 않는 것:
// - seg_000.ts 실제 생성
// - MP4 -> TS 변환
// - HTTP 제공
class HlsVodPlaylistBuilder {
public:
    HlsVodPlaylistWriteResult write_playlist(
        const HlsVodSession& session
    ) const;

    static void print_write_result(
        const HlsVodPlaylistWriteResult& result
    );

private:
    HlsVodPlaylistWriteResult validate_session(
        const HlsVodSession& session
    ) const;

    static std::int64_t calculate_target_duration_seconds(
        const HlsVodSession& session
    );

    static std::string format_extinf_seconds(std::int64_t duration_ms);

    static std::string format_program_date_time_utc(
        const std::chrono::system_clock::time_point& time
    );
};
