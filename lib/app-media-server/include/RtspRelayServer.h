#ifndef __RTSPRELAYSERVER_H__
#define __RTSPRELAYSERVER_H__

#include "CameraManager.h"

#include <gst/rtsp-server/rtsp-server.h>

#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

class H264RelayHub;

// CameraPipeline이 전달하는 encoded H.264를 RTSP로 송출하는 relay 서버다.
//
// 27강 추가 내용:
// - media-configure / prepared / unprepared 로그를 추가한다.
// - 여러 클라이언트가 같은 mount path에 접속할 때 shared media가 어떻게 동작하는지 확인한다.
//
// CameraPipeline과 GstPipeline 생명주기는 분리하지만 H264RelayHub를 통해
// encoded buffer를 전달받으므로 카메라에는 rtspsrc 하나만 연결된다.
class RtspRelayServer {
public:
    explicit RtspRelayServer(
        H264RelayHub& relay_hub,
        std::string service = "8555",
        std::string bind_address = "0.0.0.0",
        std::string advertised_host = "127.0.0.1",
        bool enabled = true);
    ~RtspRelayServer();

    RtspRelayServer(const RtspRelayServer&) = delete;
    RtspRelayServer& operator=(const RtspRelayServer&) = delete;

    // camera_id의 H264RelayHub stream을 /live/{camera_id}에 mount한다.
    // 서버가 실행 중인 상태에서도 새 mount를 추가할 수 있다.
    bool add_h264_camera(const CameraConfig& config);

    bool start();
    void stop();

    std::string make_mount_path(const std::string& camera_id) const;
    std::string make_public_url(const std::string& camera_id) const;
    bool is_enabled() const;

private:
    bool ensure_server_created_locked();
    std::string make_h264_relay_launch() const;

private:
    H264RelayHub& relay_hub_;
    std::string service_;
    std::string bind_address_;
    std::string advertised_host_;
    bool enabled_ = true;

    mutable std::mutex mutex_;

    GstRTSPServer* server_ = nullptr;
    GMainLoop* main_loop_ = nullptr;
    guint attach_id_ = 0;

    std::thread server_thread_;
    bool running_ = false;

    // 같은 provisioning 결과가 반복돼도 factory를 중복 생성하지 않는다.
    std::unordered_set<std::string> mounted_camera_ids_;
};

#endif // __RTSPRELAYSERVER_H__
