#ifndef __RTSPRELAYSERVER_H__
#define __RTSPRELAYSERVER_H__

#include "CameraManager.h"

#include <gst/rtsp-server/rtsp-server.h>

#include <mutex>
#include <string>
#include <thread>
#include <vector>

// VMS 서버가 카메라 RTSP를 다시 RTSP로 송출하기 위한 relay 서버다.
//
// 27강 추가 내용:
// - media-configure / prepared / unprepared 로그를 추가한다.
// - 여러 클라이언트가 같은 mount path에 접속할 때 shared media가 어떻게 동작하는지 확인한다.
//
// 주의:
// - 이 클래스는 녹화용 CameraPipeline과 아직 pipeline을 공유하지 않는다.
// - 27강 기준으로는 송출용 rtspsrc가 별도로 카메라에 연결된다.
class RtspRelayServer {
public:
    explicit RtspRelayServer(std::string service = "8555");
    ~RtspRelayServer();

    RtspRelayServer(const RtspRelayServer&) = delete;
    RtspRelayServer& operator=(const RtspRelayServer&) = delete;

    // H264 RTSP 입력을 받아 VMS RTSP 서버의 /live/{camera_id}로 다시 송출한다.
    bool add_h264_camera(const CameraConfig& config);

    bool start();
    void stop();

    std::string make_mount_path(const std::string& camera_id) const;
    std::string make_local_url(const std::string& camera_id) const;

private:
    bool ensure_server_created_locked();
    std::string make_h264_relay_launch(const std::string& rtsp_uri) const;
    std::string quote_for_gst_launch(const std::string& value) const;

private:
    std::string service_;

    mutable std::mutex mutex_;

    GstRTSPServer* server_ = nullptr;
    GMainLoop* main_loop_ = nullptr;
    guint attach_id_ = 0;

    std::thread server_thread_;
    bool running_ = false;

    std::vector<std::string> mounted_paths_;
};

#endif // __RTSPRELAYSERVER_H__