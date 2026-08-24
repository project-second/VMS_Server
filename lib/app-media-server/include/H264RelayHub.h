#pragma once

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include <mutex>
#include <string>
#include <unordered_map>

// CameraPipeline의 encoded H.264 appsink와 RtspRelayServer의 appsrc를 연결한다.
//
// 카메라별로 appsrc 하나를 유지한다. RtspRelayServer가 shared media factory를
// 사용하므로 같은 mount의 여러 RTSP client는 이 appsrc가 속한 media를 공유한다.
// relay가 준비되지 않았거나 중단된 동안 들어오는 buffer는 즉시 버린다.
class H264RelayHub {
public:
    H264RelayHub() = default;
    ~H264RelayHub();

    H264RelayHub(const H264RelayHub&) = delete;
    H264RelayHub& operator=(const H264RelayHub&) = delete;

    void attach_appsrc(const std::string& camera_id, GstAppSrc* appsrc);
    void detach_appsrc(const std::string& camera_id, GstAppSrc* appsrc);

    // sample의 H.264 buffer를 현재 연결된 appsrc로 전달한다.
    // appsrc가 없거나 첫 keyframe을 기다리는 동안에는 sample을 조용히 버린다.
    void publish_sample(const std::string& camera_id, GstSample* sample);

    // CameraPipeline reconnect 시 새 keyframe/caps부터 relay를 다시 시작한다.
    void reset_source(const std::string& camera_id);

private:
    struct StreamState {
        GstAppSrc* appsrc = nullptr;
        GstCaps* caps = nullptr;
        bool waiting_for_keyframe = true;
    };

    static void clear_state(StreamState& state);

private:
    std::mutex mutex_;
    std::unordered_map<std::string, StreamState> streams_;
};
