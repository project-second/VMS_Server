#include "RecordingSegmentStore.h"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

void RecordingSegmentStore::on_segment_started(
    const std::string& camera_id,
    const std::string& file_path
) {
    // 1. Segment 생성
    RecordingSegment segment;
    segment.camera_id = camera_id;
    segment.file_path = file_path;
    segment.started_at = std::chrono::system_clock::now();
    segment.status = "recording";

    // 2. 메모리 저장
    {
        std::lock_guard<std::mutex> lock(mutex_);
        segments_.push_back(segment);
    }

    // 3. 콘솔 출력
    std::cout << "[SEGMENT STARTED] "
              << "camera_id=" << camera_id
              << ", file=" << file_path
              << ", started_at=" << time_to_string(segment.started_at)
              << std::endl;
}

void RecordingSegmentStore::on_segment_finished(
    const std::string& camera_id,
    const std::string& file_path
) {
    auto ended_at = std::chrono::system_clock::now();

    std::lock_guard<std::mutex> lock(mutex_);

    // 1. 해당 Segment 찾기
    for (auto& segment : segments_) {
        if (segment.camera_id == camera_id &&
            segment.file_path == file_path &&
            segment.status == "recording") {
            segment.ended_at = ended_at;
            segment.status = "completed";

            // 2. 콘솔 출력
            std::cout << "[SEGMENT FINISHED] "
                      << "camera_id=" << camera_id
                      << ", file=" << file_path
                      << ", started_at=" << time_to_string(segment.started_at)
                      << ", ended_at=" << time_to_string(ended_at)
                      << ", status=completed"
                      << std::endl;

            return;
        }
    }

    // 3. 이미 completed였거나 찾지 못한 경우
    std::cout << "[SEGMENT FINISHED] "
              << "camera_id=" << camera_id
              << ", file=" << file_path
              << ", warning=segment not found or already closed"
              << std::endl;
}

void RecordingSegmentStore::mark_open_segments_interrupted(
    const std::string& camera_id
) {
    auto ended_at = std::chrono::system_clock::now();

    std::lock_guard<std::mutex> lock(mutex_);

    // 1. 아직 recording 상태인 segment를 interrupted로 마킹
    for (auto& segment : segments_) {
        if (segment.camera_id == camera_id &&
            segment.status == "recording") {
            segment.ended_at = ended_at;
            segment.status = "interrupted";

            std::cout << "[SEGMENT INTERRUPTED] "
                      << "camera_id=" << camera_id
                      << ", file=" << segment.file_path
                      << ", started_at=" << time_to_string(segment.started_at)
                      << ", ended_at=" << time_to_string(ended_at)
                      << ", status=interrupted"
                      << std::endl;
        }
    }
}

std::vector<RecordingSegment> RecordingSegmentStore::find_segments_by_time_range(
    const std::string& camera_id,
    const std::chrono::system_clock::time_point& from,
    const std::chrono::system_clock::time_point& to
) const {
    std::vector<RecordingSegment> result;

    // 1. 잘못된 시간 범위 방어
    if (from >= to) {
        return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    const auto now = std::chrono::system_clock::now();

    // 2. camera_id와 시간 범위가 겹치는 segment 찾기
    for (const auto& segment : segments_) {
        if (segment.camera_id != camera_id) {
            continue;
        }

        // 3. 재생 가능한 상태만 조회
        if (segment.status != "completed" &&
            segment.status != "recording" &&
            segment.status != "interrupted") {
            continue;
        }

        // 4. 아직 recording 중이면 현재 시각을 임시 종료 시각으로 사용
        auto segment_end = segment.ended_at.value_or(now);

        if (is_overlapped(
                segment.started_at,
                segment_end,
                from,
                to
            )) {
            result.push_back(segment);
        }
    }

    return result;
}

void RecordingSegmentStore::print_segments_by_time_range(
    const std::string& camera_id,
    const std::chrono::system_clock::time_point& from,
    const std::chrono::system_clock::time_point& to
) const {
    auto result = find_segments_by_time_range(
        camera_id,
        from,
        to
    );

    std::cout << std::endl;
    std::cout << "===== Segment Query Result =====" << std::endl;
    std::cout << "camera_id=" << camera_id << std::endl;
    std::cout << "from=" << time_to_string(from) << std::endl;
    std::cout << "to=" << time_to_string(to) << std::endl;
    std::cout << "count=" << result.size() << std::endl;

    for (const auto& segment : result) {
        std::cout << "- file=" << segment.file_path
                  << ", status=" << segment.status
                  << ", started_at=" << time_to_string(segment.started_at);

        if (segment.ended_at.has_value()) {
            std::cout << ", ended_at=" << time_to_string(*segment.ended_at);
        } else {
            std::cout << ", ended_at=null";
        }

        std::cout << std::endl;
    }

    std::cout << "================================" << std::endl;
}

void RecordingSegmentStore::print_all() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::cout << std::endl;
    std::cout << "===== Recording Segments =====" << std::endl;

    for (const auto& segment : segments_) {
        std::cout << "camera_id=" << segment.camera_id
                  << ", file=" << segment.file_path
                  << ", status=" << segment.status
                  << ", started_at=" << time_to_string(segment.started_at);

        if (segment.ended_at.has_value()) {
            std::cout << ", ended_at=" << time_to_string(*segment.ended_at);
        } else {
            std::cout << ", ended_at=null";
        }

        std::cout << std::endl;
    }

    std::cout << "==============================" << std::endl;
}

bool RecordingSegmentStore::is_overlapped(
    const std::chrono::system_clock::time_point& segment_start,
    const std::chrono::system_clock::time_point& segment_end,
    const std::chrono::system_clock::time_point& query_from,
    const std::chrono::system_clock::time_point& query_to
) {
    // 1. 반열린 구간 [start, end) 기준
    return segment_start < query_to && segment_end > query_from;
}

std::string RecordingSegmentStore::time_to_string(
    const std::chrono::system_clock::time_point& time
) {
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

    return oss.str();
}