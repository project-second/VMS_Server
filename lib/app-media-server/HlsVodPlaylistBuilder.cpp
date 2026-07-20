#include "HlsVodPlaylistBuilder.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>

HlsVodPlaylistWriteResult HlsVodPlaylistBuilder::write_playlist(
    const HlsVodSession& session
) const {
    // 1. HLS session 검증
    auto validation = validate_session(session);
    if (!validation.ok) {
        return validation;
    }

    // 2. cache 디렉터리 보장
    std::error_code error;
    std::filesystem::create_directories(session.cache_dir, error);
    if (error) {
        HlsVodPlaylistWriteResult result;
        result.ok = false;
        result.error_message = "failed to create HLS cache directory: " + error.message();
        return result;
    }

    // 3. TARGETDURATION 계산
    const auto target_duration_seconds =
        calculate_target_duration_seconds(session);

    // 4. index.m3u8 작성
    std::ofstream out(session.playlist_path);
    if (!out.is_open()) {
        HlsVodPlaylistWriteResult result;
        result.ok = false;
        result.error_message = "failed to open playlist file: " + session.playlist_path.string();
        return result;
    }

    out << "#EXTM3U\n";
    out << "#EXT-X-VERSION:3\n";
    out << "#EXT-X-PLAYLIST-TYPE:VOD\n";
    out << "#EXT-X-TARGETDURATION:" << target_duration_seconds << "\n";
    out << "#EXT-X-MEDIA-SEQUENCE:0\n";

    for (const auto& segment : session.segments) {
        out << "#EXT-X-PROGRAM-DATE-TIME:"
            << format_program_date_time_utc(segment.program_date_time)
            << "\n";

        out << "#EXTINF:"
            << format_extinf_seconds(segment.duration_ms)
            << ",\n";

        // m3u8 안에는 절대 경로가 아니라 playlist 기준 상대 파일명을 적는다.
        out << segment.output_file_name << "\n";
    }

    out << "#EXT-X-ENDLIST\n";

    if (!out.good()) {
        HlsVodPlaylistWriteResult result;
        result.ok = false;
        result.error_message = "failed while writing playlist file: " + session.playlist_path.string();
        return result;
    }

    HlsVodPlaylistWriteResult result;
    result.ok = true;
    result.playlist_path = session.playlist_path;
    result.segment_count = session.segments.size();
    result.target_duration_seconds = target_duration_seconds;
    return result;
}

HlsVodPlaylistWriteResult HlsVodPlaylistBuilder::validate_session(
    const HlsVodSession& session
) const {
    HlsVodPlaylistWriteResult result;

    if (session.session_id.empty()) {
        result.ok = false;
        result.error_message = "HLS session_id is empty";
        return result;
    }

    if (session.camera_id.empty()) {
        result.ok = false;
        result.error_message = "HLS camera_id is empty";
        return result;
    }

    if (session.cache_dir.empty()) {
        result.ok = false;
        result.error_message = "HLS cache_dir is empty";
        return result;
    }

    if (session.playlist_path.empty()) {
        result.ok = false;
        result.error_message = "HLS playlist_path is empty";
        return result;
    }

    if (session.segments.empty()) {
        result.ok = false;
        result.error_message = "HLS session has no segments";
        return result;
    }

    for (const auto& segment : session.segments) {
        if (segment.output_file_name.empty()) {
            result.ok = false;
            result.error_message = "HLS segment output_file_name is empty";
            return result;
        }

        if (segment.duration_ms <= 0) {
            result.ok = false;
            result.error_message = "HLS segment duration must be positive";
            return result;
        }
    }

    result.ok = true;
    return result;
}

std::int64_t HlsVodPlaylistBuilder::calculate_target_duration_seconds(
    const HlsVodSession& session
) {
    std::int64_t max_duration_ms = 0;

    for (const auto& segment : session.segments) {
        max_duration_ms = std::max(max_duration_ms, segment.duration_ms);
    }

    // HLS TARGETDURATION은 초 단위 정수다.
    // EXTINF duration을 올림해서 가장 긴 segment를 포함할 수 있게 한다.
    auto seconds = (max_duration_ms + 999) / 1000;
    if (seconds <= 0) {
        seconds = 1;
    }

    return seconds;
}

std::string HlsVodPlaylistBuilder::format_extinf_seconds(
    std::int64_t duration_ms
) {
    const auto seconds = static_cast<double>(duration_ms) / 1000.0;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3) << seconds;
    return oss.str();
}

std::string HlsVodPlaylistBuilder::format_program_date_time_utc(
    const std::chrono::system_clock::time_point& time
) {
    const auto time_t_value = std::chrono::system_clock::to_time_t(time);

    std::tm tm_value{};
#if defined(_WIN32)
    gmtime_s(&tm_value, &time_t_value);
#else
    gmtime_r(&time_t_value, &tm_value);
#endif

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        time.time_since_epoch()
    ).count() % 1000;

    std::ostringstream oss;
    oss << std::put_time(&tm_value, "%Y-%m-%dT%H:%M:%S")
        << "."
        << std::setw(3)
        << std::setfill('0')
        << ms
        << "Z";

    return oss.str();
}

void HlsVodPlaylistBuilder::print_write_result(
    const HlsVodPlaylistWriteResult& result
) {
    std::cout << std::endl;
    std::cout << "===== HlsVodPlaylistWriteResult =====" << std::endl;
    std::cout << "ok=" << (result.ok ? "true" : "false") << std::endl;

    if (!result.ok) {
        std::cout << "error=" << result.error_message << std::endl;
        std::cout << "====================================" << std::endl;
        return;
    }

    std::cout << "playlist_path=" << result.playlist_path.string() << std::endl;
    std::cout << "segment_count=" << result.segment_count << std::endl;
    std::cout << "target_duration_seconds=" << result.target_duration_seconds << std::endl;
    std::cout << "====================================" << std::endl;
}
