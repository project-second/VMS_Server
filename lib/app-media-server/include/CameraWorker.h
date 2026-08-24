#ifndef __CAMERAWORKER_H__
#define __CAMERAWORKER_H__

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "RecordingSegmentStore.h"

class H264RelayHub;

class CameraWorker {
private:
    std::int64_t profile_id_ = 0;
    std::string camera_id_;
    std::string rtsp_uri_;
    std::string record_pattern_;
    int split_seconds_ = 5;

    RecordingSegmentStore* segment_store_ = nullptr;
    H264RelayHub* relay_hub_ = nullptr;

    std::atomic_bool stop_requested_{false};
    std::thread worker_thread_;

public:
    CameraWorker(
        std::int64_t profile_id,
        std::string camera_id,
        std::string rtsp_uri,
        std::string record_pattern,
        int split_seconds,
        RecordingSegmentStore* segment_store,
        H264RelayHub* relay_hub
    );

    ~CameraWorker();

    CameraWorker(const CameraWorker&) = delete;
    CameraWorker& operator=(const CameraWorker&) = delete;

    void start();
    void stop();

    const std::string& camera_id() const;

private:
    void run();
    void sleep_retry_interval();
};

#endif // __CAMERAWORKER_H__
