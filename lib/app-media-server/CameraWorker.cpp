#include "CameraWorker.h"

#include "CameraPipeline.h"

#include <glib.h>

#include <iostream>
#include <utility>

CameraWorker::CameraWorker(
    std::string camera_id,
    std::string rtsp_uri,
    std::string record_pattern,
    int split_seconds,
    RecordingSegmentStore* segment_store
)
    : camera_id_(std::move(camera_id)),
      rtsp_uri_(std::move(rtsp_uri)),
      record_pattern_(std::move(record_pattern)),
      split_seconds_(split_seconds),
      segment_store_(segment_store) {}

CameraWorker::~CameraWorker() {
    stop();
}

void CameraWorker::start() {
    // 1. 이미 실행 중이면 무시
    if (worker_thread_.joinable()) {
        return;
    }

    // 2. stop flag 초기화
    stop_requested_ = false;

    // 3. worker thread 시작
    worker_thread_ = std::thread(&CameraWorker::run, this);
}

void CameraWorker::stop() {
    // 1. stop 요청
    stop_requested_ = true;

    // 2. thread 종료 대기
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

const std::string& CameraWorker::camera_id() const {
    return camera_id_;
}

void CameraWorker::run() {
    int retry_count = 0;

    // 1. 재연결 루프
    while (!stop_requested_) {
        retry_count++;

        std::cout << std::endl;
        std::cout << "[" << camera_id_ << "] "
                  << "===== Connect attempt "
                  << retry_count
                  << " ====="
                  << std::endl;

        // 2. CameraPipeline 생성
        CameraPipeline camera(
            camera_id_,
            rtsp_uri_,
            record_pattern_,
            split_seconds_,
            segment_store_
        );

        // 3. 파이프라인 생성
        if (!camera.create()) {
            std::cerr << "[" << camera_id_ << "] "
                      << "Failed to create camera pipeline"
                      << std::endl;

            camera.stop();
            sleep_retry_interval();
            continue;
        }

        // 4. 파이프라인 실행
        if (!camera.start()) {
            std::cerr << "[" << camera_id_ << "] "
                      << "Failed to start camera pipeline"
                      << std::endl;

            camera.stop();
            sleep_retry_interval();
            continue;
        }

        std::cout << "[" << camera_id_ << "] Camera pipeline started" << std::endl;

        // 5. ERROR/EOS 또는 stop 요청까지 반복 확인
        while (!stop_requested_) {
            CameraPipelineEvent event = camera.poll_bus(200);

            if (event == CameraPipelineEvent::None) {
                continue;
            }

            if (event == CameraPipelineEvent::Error) {
                std::cout << "[" << camera_id_ << "] "
                          << "Pipeline error detected"
                          << std::endl;
                break;
            }

            if (event == CameraPipelineEvent::Eos) {
                std::cout << "[" << camera_id_ << "] "
                          << "Pipeline EOS detected"
                          << std::endl;
                break;
            }
        }

        // 6. 파이프라인 정리
        camera.stop();

        // 7. stop 요청이면 종료
        if (stop_requested_) {
            break;
        }

        // 8. 재연결 대기
        std::cout << "[" << camera_id_ << "] "
                  << "Retry after 3 seconds..."
                  << std::endl;

        sleep_retry_interval();
    }

    std::cout << "[" << camera_id_ << "] CameraWorker stopped" << std::endl;
}

void CameraWorker::sleep_retry_interval() {
    // 1. 3초를 100ms 단위로 쪼개서 stop 요청에 빠르게 반응
    for (int i = 0; i < 30; ++i) {
        if (stop_requested_) {
            break;
        }

        g_usleep(100 * 1000);
    }
}