#include "PlaybackService.h"

#include <algorithm>
#include <atomic>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

std::int64_t milliseconds_between(
    const std::chrono::system_clock::time_point& from,
    const std::chrono::system_clock::time_point& to
) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        to - from
    ).count();
}

bool time_in_half_open_range(
    const std::chrono::system_clock::time_point& time,
    const std::chrono::system_clock::time_point& from,
    const std::chrono::system_clock::time_point& to
) {
    return time >= from && time < to;
}

} // namespace

PlaybackService::PlaybackService(const RecordingSegmentStore& segment_store)
    : segment_store_(segment_store) {
}

PlaybackResponse PlaybackService::create_session(
    const PlaybackRequest& request
) const {
    // 1. 요청값 검증
    auto validation = validate_request(request);
    if (!validation.ok) {
        return validation;
    }

    // 2. 요청 시간 범위와 겹치는 녹화 segment 조회
    auto segments = segment_store_.find_segments_by_time_range(
        request.camera_id,
        request.from,
        request.to
    );

    // 3. 시간 순서 정렬
    std::sort(
        segments.begin(),
        segments.end(),
        [](const RecordingSegment& lhs, const RecordingSegment& rhs) {
            return lhs.started_at < rhs.started_at;
        }
    );

    // 4. PlaybackSession 생성
    PlaybackSession session;
    session.session_id = make_session_id();
    session.camera_id = request.camera_id;
    session.requested_from = request.from;
    session.requested_to = request.to;
    session.created_at = std::chrono::system_clock::now();

    // 5. 녹화 segment를 playback entry로 변환
    //    HLS VOD는 고정된 과거 파일을 대상으로 하므로, 아직 recording 중이고
    //    ended_at이 없는 segment는 이번 단계에서는 제외한다.
    for (const auto& segment : segments) {
        auto entry = make_entry_from_segment(
            segment,
            request
        );

        if (!entry.has_value()) {
            continue;
        }

        session.total_duration_ms += entry->play_duration_ms;
        session.entries.push_back(*entry);
    }

    // 6. 재생 가능한 segment가 없으면 실패 응답
    if (session.entries.empty()) {
        PlaybackResponse response;
        response.ok = false;
        response.error_message = "no playable recording segments for requested range";
        return response;
    }

    // 7. 성공 응답
    PlaybackResponse response;
    response.ok = true;
    response.session = session;
    return response;
}

PlaybackSeekResult PlaybackService::seek(
    const PlaybackSession& session,
    const PlaybackSeekRequest& request
) const {
    PlaybackSeekResult result;
    result.session_id = session.session_id;
    result.camera_id = session.camera_id;
    result.seek_time = request.seek_time;

    // 1. session id 검증
    if (!request.session_id.empty() && request.session_id != session.session_id) {
        result.ok = false;
        result.error_message = "session_id does not match playback session";
        return result;
    }

    // 2. session 상태 검증
    if (session.entries.empty()) {
        result.ok = false;
        result.error_message = "playback session has no entries";
        return result;
    }

    // 3. 요청한 원래 playback 범위 기준 검증
    //    seek_time == requested_to는 재생할 sample이 없는 끝 지점이므로 실패로 본다.
    if (!time_in_half_open_range(
            request.seek_time,
            session.requested_from,
            session.requested_to
        )) {
        result.ok = false;
        result.error_message = "seek_time is outside requested playback range";
        return result;
    }

    // 4. entry를 순회하면서 seek_time을 포함하는 재생 구간을 찾는다.
    //    session_playback_offset_ms는 실제 HLS 재생 흐름 기준 offset이다.
    //    녹화 공백이 있으면 absolute timeline offset과 달라질 수 있다.
    std::int64_t accumulated_playback_ms = 0;

    for (std::size_t index = 0; index < session.entries.size(); ++index) {
        const auto& entry = session.entries[index];

        if (!time_in_half_open_range(
                request.seek_time,
                entry.play_from,
                entry.play_to
            )) {
            accumulated_playback_ms += entry.play_duration_ms;
            continue;
        }

        const auto offset_inside_entry_ms = milliseconds_between(
            entry.play_from,
            request.seek_time
        );

        result.ok = true;
        result.entry_index = index;
        result.file_path = entry.file_path;

        // 원본 파일 내부 offset이다.
        // entry.play_from_offset_ms는 segment 시작 기준 entry 시작 offset이고,
        // offset_inside_entry_ms는 entry 시작 기준 seek offset이다.
        result.file_offset_ms = entry.play_from_offset_ms + offset_inside_entry_ms;

        // 사용자가 요청한 절대시간 범위에서의 offset이다.
        // CCTV 타임라인 표시에는 이 값이 직관적이다.
        result.absolute_timeline_offset_ms = milliseconds_between(
            session.requested_from,
            request.seek_time
        );

        // 실제 HLS VOD 재생 흐름 기준 offset이다.
        // 녹화 공백이 있을 때 player.currentTime과 맞추기 좋다.
        result.session_playback_offset_ms = accumulated_playback_ms + offset_inside_entry_ms;

        result.remaining_entries = make_remaining_entries_from_seek(
            session,
            index,
            request.seek_time,
            result.file_offset_ms
        );
        result.remaining_duration_ms = total_duration_ms(result.remaining_entries);

        return result;
    }

    // 5. requested_from~requested_to 안에는 있지만 실제 녹화 entry에는 없는 경우다.
    //    녹화 공백 구간으로 보면 된다.
    result.ok = false;
    result.error_message = "seek_time is in a recording gap";
    result.absolute_timeline_offset_ms = milliseconds_between(
        session.requested_from,
        request.seek_time
    );
    result.session_playback_offset_ms = accumulated_playback_ms;
    return result;
}

void PlaybackService::print_response(const PlaybackResponse& response) {
    std::cout << std::endl;
    std::cout << "===== PlaybackService Response =====" << std::endl;

    if (!response.ok) {
        std::cout << "ok=false" << std::endl;
        std::cout << "error=" << response.error_message << std::endl;
        std::cout << "====================================" << std::endl;
        return;
    }

    if (!response.session.has_value()) {
        std::cout << "ok=false" << std::endl;
        std::cout << "error=session missing" << std::endl;
        std::cout << "====================================" << std::endl;
        return;
    }

    const auto& session = *response.session;

    std::cout << "ok=true" << std::endl;
    std::cout << "session_id=" << session.session_id << std::endl;
    std::cout << "camera_id=" << session.camera_id << std::endl;
    std::cout << "requested_from=" << time_to_string(session.requested_from) << std::endl;
    std::cout << "requested_to=" << time_to_string(session.requested_to) << std::endl;
    std::cout << "entry_count=" << session.entries.size() << std::endl;
    std::cout << "total_duration_ms=" << session.total_duration_ms << std::endl;
    std::cout << std::endl;

    for (std::size_t index = 0; index < session.entries.size(); ++index) {
        const auto& entry = session.entries[index];

        std::cout << "  [Entry " << index << "]" << std::endl;
        std::cout << "    file_path=" << entry.file_path << std::endl;
        std::cout << "    segment_started_at=" << time_to_string(entry.segment_started_at) << std::endl;
        std::cout << "    segment_ended_at=" << time_to_string(entry.segment_ended_at) << std::endl;
        std::cout << "    play_from=" << time_to_string(entry.play_from) << std::endl;
        std::cout << "    play_to=" << time_to_string(entry.play_to) << std::endl;
        std::cout << "    play_from_offset_ms=" << entry.play_from_offset_ms << std::endl;
        std::cout << "    play_duration_ms=" << entry.play_duration_ms << std::endl;
        std::cout << "    source_status=" << entry.source_status << std::endl;
    }

    std::cout << "====================================" << std::endl;
}

void PlaybackService::print_seek_result(const PlaybackSeekResult& result) {
    std::cout << std::endl;
    std::cout << "===== PlaybackSeekResult =====" << std::endl;
    std::cout << "session_id=" << result.session_id << std::endl;
    std::cout << "camera_id=" << result.camera_id << std::endl;
    std::cout << "seek_time=" << time_to_string(result.seek_time) << std::endl;

    if (!result.ok) {
        std::cout << "ok=false" << std::endl;
        std::cout << "error=" << result.error_message << std::endl;
        std::cout << "absolute_timeline_offset_ms=" << result.absolute_timeline_offset_ms << std::endl;
        std::cout << "session_playback_offset_ms=" << result.session_playback_offset_ms << std::endl;
        std::cout << "==============================" << std::endl;
        return;
    }

    std::cout << "ok=true" << std::endl;
    std::cout << "entry_index=" << result.entry_index << std::endl;
    std::cout << "file_path=" << result.file_path << std::endl;
    std::cout << "file_offset_ms=" << result.file_offset_ms << std::endl;
    std::cout << "absolute_timeline_offset_ms=" << result.absolute_timeline_offset_ms << std::endl;
    std::cout << "session_playback_offset_ms=" << result.session_playback_offset_ms << std::endl;
    std::cout << "remaining_entry_count=" << result.remaining_entries.size() << std::endl;
    std::cout << "remaining_duration_ms=" << result.remaining_duration_ms << std::endl;

    for (std::size_t index = 0; index < result.remaining_entries.size(); ++index) {
        const auto& entry = result.remaining_entries[index];

        std::cout << "  [Remaining Entry " << index << "]" << std::endl;
        std::cout << "    file_path=" << entry.file_path << std::endl;
        std::cout << "    play_from=" << time_to_string(entry.play_from) << std::endl;
        std::cout << "    play_to=" << time_to_string(entry.play_to) << std::endl;
        std::cout << "    play_from_offset_ms=" << entry.play_from_offset_ms << std::endl;
        std::cout << "    play_duration_ms=" << entry.play_duration_ms << std::endl;
    }

    std::cout << "==============================" << std::endl;
}

PlaybackResponse PlaybackService::validate_request(
    const PlaybackRequest& request
) const {
    PlaybackResponse response;

    if (request.camera_id.empty()) {
        response.ok = false;
        response.error_message = "camera_id is empty";
        return response;
    }

    if (request.from >= request.to) {
        response.ok = false;
        response.error_message = "invalid playback time range";
        return response;
    }

    response.ok = true;
    return response;
}

std::string PlaybackService::make_session_id() {
    static std::atomic<std::uint64_t> counter{0};

    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();

    std::ostringstream oss;
    oss << "session_" << millis << "_" << counter.fetch_add(1);

    return oss.str();
}

std::optional<PlaybackEntry> PlaybackService::make_entry_from_segment(
    const RecordingSegment& segment,
    const PlaybackRequest& request
) {
    // 1. HLS VOD는 완료된 파일을 대상으로 한다.
    //    recording 상태처럼 아직 파일이 닫히지 않은 segment는 이번 단계에서 제외한다.
    if (!segment.ended_at.has_value()) {
        return std::nullopt;
    }

    if (segment.status != "completed" &&
        segment.status != "interrupted") {
        return std::nullopt;
    }

    const auto segment_end = *segment.ended_at;

    // 2. 요청 범위와 실제 segment 범위의 교집합을 구한다.
    const auto play_from = std::max(
        segment.started_at,
        request.from
    );

    const auto play_to = std::min(
        segment_end,
        request.to
    );

    if (play_from >= play_to) {
        return std::nullopt;
    }

    // 3. 파일 내부 offset과 실제 재생 길이를 계산한다.
    PlaybackEntry entry;
    entry.file_path = segment.file_path;
    entry.segment_started_at = segment.started_at;
    entry.segment_ended_at = segment_end;
    entry.play_from = play_from;
    entry.play_to = play_to;
    entry.play_from_offset_ms = milliseconds_between(
        segment.started_at,
        play_from
    );
    entry.play_duration_ms = milliseconds_between(
        play_from,
        play_to
    );
    entry.source_status = segment.status;

    if (entry.play_duration_ms <= 0) {
        return std::nullopt;
    }

    return entry;
}

std::vector<PlaybackEntry> PlaybackService::make_remaining_entries_from_seek(
    const PlaybackSession& session,
    std::size_t entry_index,
    const std::chrono::system_clock::time_point& seek_time,
    std::int64_t file_offset_ms
) {
    std::vector<PlaybackEntry> remaining_entries;

    if (entry_index >= session.entries.size()) {
        return remaining_entries;
    }

    // 1. 첫 entry는 seek_time부터 시작하도록 잘라낸다.
    PlaybackEntry first = session.entries[entry_index];
    first.play_from = seek_time;
    first.play_from_offset_ms = file_offset_ms;
    first.play_duration_ms = milliseconds_between(
        first.play_from,
        first.play_to
    );

    if (first.play_duration_ms > 0) {
        remaining_entries.push_back(first);
    }

    // 2. 이후 entry는 원래 session 순서를 그대로 유지한다.
    for (std::size_t index = entry_index + 1; index < session.entries.size(); ++index) {
        remaining_entries.push_back(session.entries[index]);
    }

    return remaining_entries;
}

std::int64_t PlaybackService::total_duration_ms(
    const std::vector<PlaybackEntry>& entries
) {
    std::int64_t total = 0;

    for (const auto& entry : entries) {
        total += entry.play_duration_ms;
    }

    return total;
}

std::string PlaybackService::time_to_string(
    const std::chrono::system_clock::time_point& time
) {
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        time.time_since_epoch()
    ).count() % 1000;

    std::time_t tt = std::chrono::system_clock::to_time_t(time);

    std::tm tm_value{};

#if defined(_WIN32)
    localtime_s(&tm_value, &tt);
#else
    localtime_r(&tt, &tm_value);
#endif

    std::ostringstream oss;
    oss << std::put_time(
        &tm_value,
        "%Y-%m-%d %H:%M:%S"
    );
    oss << "." << std::setw(3) << std::setfill('0') << millis;

    return oss.str();
}
