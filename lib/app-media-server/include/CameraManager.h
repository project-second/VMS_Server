#ifndef __CAMERAMANAGER_H__
#define __CAMERAMANAGER_H__

#include "CameraWorker.h"
#include "RecordingSegmentStore.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct CameraConfig {
    std::string camera_id;
    std::string rtsp_uri;
    std::string record_pattern;
    int split_seconds = 5;
};

class CameraManager {
private:
    bool running_ = false;

    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<CameraWorker>> workers_;

    RecordingSegmentStore segment_store_;

public:
    CameraManager() = default;
    ~CameraManager();

    CameraManager(const CameraManager&) = delete;
    CameraManager& operator=(const CameraManager&) = delete;

    bool add_camera(const CameraConfig& config);
    bool remove_camera(const std::string& camera_id);

    void start_all();
    void stop_all();

    void print_segments() const;

    void print_segments_by_time_range(
        const std::string& camera_id,
        const std::chrono::system_clock::time_point& from,
        const std::chrono::system_clock::time_point& to
    ) const;

    std::size_t camera_count() const;
    bool is_running() const;

private:
    bool has_camera_id_locked(const std::string& camera_id) const;
};

#endif // __CAMERAMANAGER_H__