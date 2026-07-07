#pragma once

#include "CameraWorker.h"
#include "RecordingSegmentStore.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// GStreamer 녹화 엔진이 카메라 1대를 실행하기 위해 필요한 최소 설정이다.
//
// 주의:
// - 이 구조체는 ONVIF MediaProfile이 아니다.
// - profile token, PTZ token, GetStreamUri 과정은 ONVIF/DB/서비스 계층에서 관리한다.
// - 이 모듈은 최종적으로 선택된 RTSP URI만 받아 녹화한다.
struct CameraConfig {
    std::string camera_id;
    std::string rtsp_uri;
    std::string record_pattern;
    int split_seconds = 5;
};

class CameraManager {
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

private:
    bool running_ = false;

    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<CameraWorker>> workers_;

    RecordingSegmentStore segment_store_;
};
