#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct RecordingSegment {
    std::int64_t profile_id = 0;
    std::string camera_id;
    std::string file_path;

    std::chrono::system_clock::time_point started_at;
    std::optional<std::chrono::system_clock::time_point> ended_at;

    std::string status;
};

class RecordingSegmentStore {
public:
    using SegmentChangeCallback = std::function<void(const RecordingSegment&)>;

private:
    mutable std::mutex mutex_;
    std::vector<RecordingSegment> segments_;
    SegmentChangeCallback change_callback_;
    
public:
    void set_change_callback(SegmentChangeCallback callback);

    void on_segment_started(
        std::int64_t profile_id,
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