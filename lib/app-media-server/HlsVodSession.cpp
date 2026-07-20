#include "HlsVodSession.h"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>
#include <utility>

HlsVodSessionManager::HlsVodSessionManager(
    std::filesystem::path cache_root
)
    : cache_root_(std::move(cache_root)) {
}

HlsVodSessionCreateResult HlsVodSessionManager::create_session(
    const PlaybackSession& playback_session
) const {
    // 1. PlaybackSession 검증
    auto validation = validate_playback_session(playback_session);
    if (!validation.ok) {
        return validation;
    }

    // 2. cache root 생성
    std::error_code error;
    std::filesystem::create_directories(cache_root_, error);
    if (error) {
        HlsVodSessionCreateResult result;
        result.ok = false;
        result.error_message = "failed to create playback cache root: " + error.message();
        return result;
    }

    // 3. HLS session metadata 구성
    HlsVodSession session;
    session.session_id = playback_session.session_id;
    session.camera_id = playback_session.camera_id;
    session.requested_from = playback_session.requested_from;
    session.requested_to = playback_session.requested_to;
    session.created_at = std::chrono::system_clock::now();
    session.cache_dir = cache_root_ / playback_session.session_id;
    session.playlist_path = session.cache_dir / session.playlist_file_name;
    session.total_duration_ms = playback_session.total_duration_ms;

    // 4. session directory 생성
    std::filesystem::create_directories(session.cache_dir, error);
    if (error) {
        HlsVodSessionCreateResult result;
        result.ok = false;
        result.error_message = "failed to create HLS session directory: " + error.message();
        return result;
    }

    // 5. PlaybackEntry를 HlsVodSegment 계획으로 변환
    for (std::size_t index = 0; index < playback_session.entries.size(); ++index) {
        const auto& entry = playback_session.entries[index];
        session.segments.push_back(
            make_segment(entry, index, session.cache_dir)
        );
    }

    // 6. 33강 확인용 manifest 작성
    //    실제 HLS playlist가 아니라, 이번 단계에서 생성된 session 계획을 확인하기 위한 파일이다.
    std::string manifest_error;
    if (!write_debug_manifest(session, manifest_error)) {
        HlsVodSessionCreateResult result;
        result.ok = false;
        result.error_message = manifest_error;
        return result;
    }

    HlsVodSessionCreateResult result;
    result.ok = true;
    result.session = session;
    return result;
}

const std::filesystem::path& HlsVodSessionManager::cache_root() const {
    return cache_root_;
}

HlsVodSessionCreateResult HlsVodSessionManager::validate_playback_session(
    const PlaybackSession& playback_session
) const {
    HlsVodSessionCreateResult result;

    if (playback_session.session_id.empty()) {
        result.ok = false;
        result.error_message = "playback session_id is empty";
        return result;
    }

    if (playback_session.camera_id.empty()) {
        result.ok = false;
        result.error_message = "playback camera_id is empty";
        return result;
    }

    if (playback_session.requested_from >= playback_session.requested_to) {
        result.ok = false;
        result.error_message = "playback requested_from must be earlier than requested_to";
        return result;
    }

    if (playback_session.entries.empty()) {
        result.ok = false;
        result.error_message = "playback session has no entries";
        return result;
    }

    result.ok = true;
    return result;
}

HlsVodSegment HlsVodSessionManager::make_segment(
    const PlaybackEntry& entry,
    std::size_t sequence_no,
    const std::filesystem::path& cache_dir
) const {
    HlsVodSegment segment;
    segment.sequence_no = sequence_no;
    segment.source_file_path = entry.file_path;
    segment.source_offset_ms = entry.play_from_offset_ms;
    segment.duration_ms = entry.play_duration_ms;
    segment.program_date_time = entry.play_from;
    segment.output_file_name = make_segment_file_name(sequence_no);
    segment.output_file_path = cache_dir / segment.output_file_name;
    return segment;
}

bool HlsVodSessionManager::write_debug_manifest(
    const HlsVodSession& session,
    std::string& error_message
) const {
    const auto manifest_path = session.cache_dir / "session_info.txt";

    std::ofstream out(manifest_path);
    if (!out.is_open()) {
        error_message = "failed to open debug manifest: " + manifest_path.string();
        return false;
    }

    out << "HLS VOD Session Plan\n";
    out << "====================\n";
    out << "session_id=" << session.session_id << "\n";
    out << "camera_id=" << session.camera_id << "\n";
    out << "requested_from=" << time_to_string(session.requested_from) << "\n";
    out << "requested_to=" << time_to_string(session.requested_to) << "\n";
    out << "created_at=" << time_to_string(session.created_at) << "\n";
    out << "cache_dir=" << session.cache_dir.string() << "\n";
    out << "playlist_path=" << session.playlist_path.string() << "\n";
    out << "segment_count=" << session.segments.size() << "\n";
    out << "total_duration_ms=" << session.total_duration_ms << "\n";
    out << "\n";

    for (const auto& segment : session.segments) {
        out << "[segment " << segment.sequence_no << "]\n";
        out << "source_file_path=" << segment.source_file_path << "\n";
        out << "source_offset_ms=" << segment.source_offset_ms << "\n";
        out << "duration_ms=" << segment.duration_ms << "\n";
        out << "program_date_time=" << time_to_string(segment.program_date_time) << "\n";
        out << "output_file_name=" << segment.output_file_name << "\n";
        out << "output_file_path=" << segment.output_file_path.string() << "\n";
        out << "\n";
    }

    return true;
}

std::string HlsVodSessionManager::make_segment_file_name(
    std::size_t sequence_no
) {
    std::ostringstream oss;
    oss << "seg_"
        << std::setw(3)
        << std::setfill('0')
        << sequence_no
        << ".ts";
    return oss.str();
}

void HlsVodSessionManager::print_create_result(
    const HlsVodSessionCreateResult& result
) {
    std::cout << std::endl;
    std::cout << "===== HlsVodSessionCreateResult =====" << std::endl;
    std::cout << "ok=" << (result.ok ? "true" : "false") << std::endl;

    if (!result.ok) {
        std::cout << "error=" << result.error_message << std::endl;
        std::cout << "====================================" << std::endl;
        return;
    }

    if (!result.session.has_value()) {
        std::cout << "error=session is empty" << std::endl;
        std::cout << "====================================" << std::endl;
        return;
    }

    const auto& session = *result.session;
    std::cout << "session_id=" << session.session_id << std::endl;
    std::cout << "camera_id=" << session.camera_id << std::endl;
    std::cout << "requested_from=" << time_to_string(session.requested_from) << std::endl;
    std::cout << "requested_to=" << time_to_string(session.requested_to) << std::endl;
    std::cout << "cache_dir=" << session.cache_dir.string() << std::endl;
    std::cout << "playlist_path=" << session.playlist_path.string() << std::endl;
    std::cout << "segment_count=" << session.segments.size() << std::endl;
    std::cout << "total_duration_ms=" << session.total_duration_ms << std::endl;

    for (const auto& segment : session.segments) {
        std::cout << std::endl;
        std::cout << "  [HLS Segment Plan " << segment.sequence_no << "]" << std::endl;
        std::cout << "    source_file_path=" << segment.source_file_path << std::endl;
        std::cout << "    source_offset_ms=" << segment.source_offset_ms << std::endl;
        std::cout << "    duration_ms=" << segment.duration_ms << std::endl;
        std::cout << "    program_date_time=" << time_to_string(segment.program_date_time) << std::endl;
        std::cout << "    output_file_name=" << segment.output_file_name << std::endl;
        std::cout << "    output_file_path=" << segment.output_file_path.string() << std::endl;
    }

    std::cout << "====================================" << std::endl;
}

std::string HlsVodSessionManager::time_to_string(
    const std::chrono::system_clock::time_point& time
) {
    const auto time_t_value = std::chrono::system_clock::to_time_t(time);

    std::tm tm_value{};
#if defined(_WIN32)
    localtime_s(&tm_value, &time_t_value);
#else
    localtime_r(&time_t_value, &tm_value);
#endif

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        time.time_since_epoch()
    ).count() % 1000;

    std::ostringstream oss;
    oss << std::put_time(&tm_value, "%Y-%m-%d %H:%M:%S")
        << "."
        << std::setw(3)
        << std::setfill('0')
        << ms;

    return oss.str();
}
