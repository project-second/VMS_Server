#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct RecordingSegment {
    std::string camera_id;
    std::string file_path;

    std::chrono::system_clock::time_point started_at;
    std::optional<std::chrono::system_clock::time_point> ended_at;

    std::string status;
};

class RecordingSegmentStore {
private:
    mutable std::mutex mutex_;
    std::vector<RecordingSegment> segments_;
    
public:
    void on_segment_started(
        const std::string& camera_id,
        const std::string& file_path
    );

    void on_segment_finished(
        const std::string& camera_id,
        const std::string& file_path
    );

    void mark_open_segments_interrupted(
        const std::string& camera_id
    );

    std::vector<RecordingSegment> find_segments_by_time_range(
        const std::string& camera_id,
        const std::chrono::system_clock::time_point& from,
        const std::chrono::system_clock::time_point& to
    ) const;

    void print_segments_by_time_range(
        const std::string& camera_id,
        const std::chrono::system_clock::time_point& from,
        const std::chrono::system_clock::time_point& to
    ) const;

    void print_all() const;

private:
    static bool is_overlapped(
        const std::chrono::system_clock::time_point& segment_start,
        const std::chrono::system_clock::time_point& segment_end,
        const std::chrono::system_clock::time_point& query_from,
        const std::chrono::system_clock::time_point& query_to
    );

    static std::string time_to_string(
        const std::chrono::system_clock::time_point& time
    );
};