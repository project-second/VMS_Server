#include "RtspRelayServer.h"

#include <glib.h>

#include <iostream>
#include <sstream>
#include <utility>

namespace {

// media-configure 콜백과 prepared/unprepared 콜백에서 함께 사용할 로그용 context다.
// factory의 signal data로 붙이며 factory가 해제될 때 함께 delete된다.
struct RelayFactoryLogContext {
    std::string camera_id;
    std::string mount_path;
    std::string input_uri;
    std::string output_url;
};

void destroy_relay_factory_log_context(
    gpointer data,
    GClosure* /*closure*/
) {
    delete static_cast<RelayFactoryLogContext*>(data);
}

const char* bool_to_text(gboolean value) {
    return value ? "true" : "false";
}

void on_media_prepared(GstRTSPMedia* media, gpointer user_data) {
    const auto* context = static_cast<const RelayFactoryLogContext*>(user_data);

    std::cout << "RtspRelayServer: media prepared" << std::endl;
    std::cout << "  camera_id : " << context->camera_id << std::endl;
    std::cout << "  mount     : " << context->mount_path << std::endl;
    std::cout << "  media ptr : " << media << std::endl;
    std::cout << "  shared    : " << bool_to_text(gst_rtsp_media_is_shared(media)) << std::endl;

    // media 내부의 GstElement를 확인해 실제 server-side pipeline 객체도 로그로 남긴다.
    // 반환 객체는 ref-count가 증가된 상태이므로 사용 후 unref한다.
    GstElement* element = gst_rtsp_media_get_element(media);
    if (element != nullptr) {
        gchar* element_name = gst_element_get_name(element);
        std::cout << "  pipeline  : " << (element_name ? element_name : "unknown") << std::endl;

        if (element_name != nullptr) {
            g_free(element_name);
        }

        gst_object_unref(element);
    }
}

void on_media_unprepared(GstRTSPMedia* media, gpointer user_data) {
    const auto* context = static_cast<const RelayFactoryLogContext*>(user_data);

    std::cout << "RtspRelayServer: media unprepared" << std::endl;
    std::cout << "  camera_id : " << context->camera_id << std::endl;
    std::cout << "  mount     : " << context->mount_path << std::endl;
    std::cout << "  media ptr : " << media << std::endl;
}

void on_media_configure(
    GstRTSPMediaFactory* /*factory*/,
    GstRTSPMedia* media,
    gpointer user_data
) {
    auto* context = static_cast<RelayFactoryLogContext*>(user_data);

    std::cout << "RtspRelayServer: media-configure" << std::endl;
    std::cout << "  camera_id : " << context->camera_id << std::endl;
    std::cout << "  mount     : " << context->mount_path << std::endl;
    std::cout << "  input     : " << context->input_uri << std::endl;
    std::cout << "  output    : " << context->output_url << std::endl;
    std::cout << "  media ptr : " << media << std::endl;
    std::cout << "  shared    : " << bool_to_text(gst_rtsp_media_is_shared(media)) << std::endl;

    // 같은 media ptr이 재사용되는지 확인하기 위해 prepared/unprepared 시점도 로그로 남긴다.
    // context는 factory signal data로 유지되므로 여기서는 별도 destroy notify를 주지 않는다.
    g_signal_connect(media, "prepared", G_CALLBACK(on_media_prepared), context);
    g_signal_connect(media, "unprepared", G_CALLBACK(on_media_unprepared), context);
}

} // namespace

RtspRelayServer::RtspRelayServer(std::string service)
    : service_(std::move(service)) {}

RtspRelayServer::~RtspRelayServer() {
    stop();
}

bool RtspRelayServer::add_h264_camera(const CameraConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_) {
        std::cerr << "RtspRelayServer: cannot add camera while server is running. "
                  << "Stop server first. camera_id=" << config.camera_id
                  << std::endl;
        return false;
    }

    if (config.camera_id.empty()) {
        std::cerr << "RtspRelayServer: camera_id is empty" << std::endl;
        return false;
    }

    if (config.rtsp_uri.empty()) {
        std::cerr << "RtspRelayServer: rtsp_uri is empty. camera_id="
                  << config.camera_id
                  << std::endl;
        return false;
    }

    if (!ensure_server_created_locked()) {
        return false;
    }

    const std::string mount_path = make_mount_path(config.camera_id);
    const std::string output_url = make_local_url(config.camera_id);
    const std::string launch = make_h264_relay_launch(config.rtsp_uri);

    GstRTSPMountPoints* mounts = gst_rtsp_server_get_mount_points(server_);
    if (mounts == nullptr) {
        std::cerr << "RtspRelayServer: failed to get mount points" << std::endl;
        return false;
    }

    GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();
    if (factory == nullptr) {
        std::cerr << "RtspRelayServer: failed to create media factory" << std::endl;
        g_object_unref(mounts);
        return false;
    }

    gst_rtsp_media_factory_set_launch(factory, launch.c_str());

    // 27강의 핵심 옵션이다.
    // TRUE이면 같은 mount path의 여러 클라이언트가 같은 GstRTSPMedia를 공유한다.
    gst_rtsp_media_factory_set_shared(factory, TRUE);

    // RTSP media 생성/준비/해제 시점을 확인하기 위한 로그 콜백을 연결한다.
    auto* context = new RelayFactoryLogContext{
        config.camera_id,
        mount_path,
        config.rtsp_uri,
        output_url
    };

    g_signal_connect_data(
        factory,
        "media-configure",
        G_CALLBACK(on_media_configure),
        context,
        destroy_relay_factory_log_context,
        static_cast<GConnectFlags>(0)
    );

    // mount point가 factory의 소유권을 가져간다.
    gst_rtsp_mount_points_add_factory(mounts, mount_path.c_str(), factory);
    g_object_unref(mounts);

    mounted_paths_.push_back(mount_path);

    std::cout << "RtspRelayServer: added H264 relay" << std::endl;
    std::cout << "  camera_id: " << config.camera_id << std::endl;
    std::cout << "  input    : " << config.rtsp_uri << std::endl;
    std::cout << "  output   : " << output_url << std::endl;
    std::cout << "  shared   : true" << std::endl;
    std::cout << "  launch   : " << launch << std::endl;

    return true;
}

bool RtspRelayServer::start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_) {
        return true;
    }

    if (!ensure_server_created_locked()) {
        return false;
    }

    main_loop_ = g_main_loop_new(nullptr, FALSE);
    if (main_loop_ == nullptr) {
        std::cerr << "RtspRelayServer: failed to create GMainLoop" << std::endl;
        return false;
    }

    attach_id_ = gst_rtsp_server_attach(server_, nullptr);
    if (attach_id_ == 0) {
        std::cerr << "RtspRelayServer: failed to attach RTSP server. "
                  << "Check whether port " << service_ << " is already in use."
                  << std::endl;

        g_main_loop_unref(main_loop_);
        main_loop_ = nullptr;
        return false;
    }

    running_ = true;

    server_thread_ = std::thread([this]() {
        std::cout << "RtspRelayServer: main loop started on port "
                  << service_ << std::endl;
        g_main_loop_run(main_loop_);
        std::cout << "RtspRelayServer: main loop stopped" << std::endl;
    });

    std::cout << "RtspRelayServer started" << std::endl;
    for (const auto& path : mounted_paths_) {
        std::cout << "  rtsp://127.0.0.1:" << service_ << path << std::endl;
    }

    return true;
}

void RtspRelayServer::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!running_ && server_ == nullptr && main_loop_ == nullptr) {
            return;
        }

        if (main_loop_ != nullptr) {
            g_main_loop_quit(main_loop_);
        }
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (attach_id_ != 0) {
        g_source_remove(attach_id_);
        attach_id_ = 0;
    }

    if (main_loop_ != nullptr) {
        g_main_loop_unref(main_loop_);
        main_loop_ = nullptr;
    }

    if (server_ != nullptr) {
        g_object_unref(server_);
        server_ = nullptr;
    }

    mounted_paths_.clear();
    running_ = false;

    std::cout << "RtspRelayServer stopped" << std::endl;
}

std::string RtspRelayServer::make_mount_path(const std::string& camera_id) const {
    return "/live/" + camera_id;
}

std::string RtspRelayServer::make_local_url(const std::string& camera_id) const {
    return "rtsp://127.0.0.1:" + service_ + make_mount_path(camera_id);
}

bool RtspRelayServer::ensure_server_created_locked() {
    if (server_ != nullptr) {
        return true;
    }

    server_ = gst_rtsp_server_new();
    if (server_ == nullptr) {
        std::cerr << "RtspRelayServer: failed to create RTSP server" << std::endl;
        return false;
    }

    g_object_set(server_, "service", service_.c_str(), nullptr);
    return true;
}

std::string RtspRelayServer::make_h264_relay_launch(const std::string& rtsp_uri) const {
    // 27강 기준 H264 RTSP relay 파이프라인.
    //
    // rtspsrc로 카메라 RTSP를 받고,
    // H264 RTP를 depay/parse한 뒤,
    // 다시 rtph264pay로 감싸 RTSP server가 클라이언트에게 내보낸다.
    //
    // decode/re-encode를 하지 않으므로 CPU 부하가 낮다.
    std::ostringstream oss;

    oss << "( "
        << "rtspsrc location=" << quote_for_gst_launch(rtsp_uri)
        << " latency=100 protocols=tcp "
        << "! rtph264depay "
        << "! h264parse config-interval=1 "
        << "! rtph264pay name=pay0 pt=96 "
        << ")";

    return oss.str();
}

std::string RtspRelayServer::quote_for_gst_launch(const std::string& value) const {
    std::string result;
    result.reserve(value.size() + 2);

    result.push_back('"');

    for (char ch : value) {
        if (ch == '\\' || ch == '"') {
            result.push_back('\\');
        }
        result.push_back(ch);
    }

    result.push_back('"');
    return result;
}
