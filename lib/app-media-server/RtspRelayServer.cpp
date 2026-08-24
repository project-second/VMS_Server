#include "RtspRelayServer.h"

#include "H264RelayHub.h"

#include <glib.h>

#include <iostream>
#include <utility>

namespace {

struct RelayFactoryLogContext {
    H264RelayHub* relay_hub = nullptr;
    std::string camera_id;
    std::string mount_path;
    std::string output_url;
};

struct RelayMediaContext {
    H264RelayHub* relay_hub = nullptr;
    std::string camera_id;
    std::string mount_path;
    GstAppSrc* appsrc = nullptr;
    bool attached = false;
};

constexpr const char* kRelayMediaContextKey = "vms-h264-relay-media-context";

void destroy_relay_factory_log_context(
    gpointer data,
    GClosure* /*closure*/
) {
    delete static_cast<RelayFactoryLogContext*>(data);
}

const char* bool_to_text(gboolean value) {
    return value ? "true" : "false";
}

void attach_relay_media_context(RelayMediaContext* context) {
    if (context == nullptr || context->attached ||
        context->relay_hub == nullptr || context->appsrc == nullptr) {
        return;
    }

    context->relay_hub->attach_appsrc(context->camera_id, context->appsrc);
    context->attached = true;
}

void detach_relay_media_context(RelayMediaContext* context) {
    if (context == nullptr || !context->attached ||
        context->relay_hub == nullptr || context->appsrc == nullptr) {
        return;
    }

    context->relay_hub->detach_appsrc(context->camera_id, context->appsrc);
    context->attached = false;
}

void destroy_relay_media_context(gpointer data) {
    auto* context = static_cast<RelayMediaContext*>(data);
    if (context == nullptr) {
        return;
    }

    detach_relay_media_context(context);

    if (context->appsrc != nullptr) {
        gst_object_unref(context->appsrc);
        context->appsrc = nullptr;
    }

    delete context;
}

void on_media_prepared(GstRTSPMedia* media, gpointer user_data) {
    auto* context = static_cast<RelayMediaContext*>(user_data);

    attach_relay_media_context(context);

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
    auto* context = static_cast<RelayMediaContext*>(user_data);

    detach_relay_media_context(context);

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
    std::cout << "  input     : CameraPipeline encoded H264 bridge" << std::endl;
    std::cout << "  output    : " << context->output_url << std::endl;
    std::cout << "  media ptr : " << media << std::endl;
    std::cout << "  shared    : " << bool_to_text(gst_rtsp_media_is_shared(media)) << std::endl;

    GstElement* media_element = gst_rtsp_media_get_element(media);
    if (media_element == nullptr || !GST_IS_BIN(media_element)) {
        std::cerr << "RtspRelayServer: media pipeline is not a GstBin. camera_id="
                  << context->camera_id
                  << std::endl;

        if (media_element != nullptr) {
            gst_object_unref(media_element);
        }
        return;
    }

    GstElement* appsrc_element = gst_bin_get_by_name(
        GST_BIN(media_element),
        "relay-source"
    );
    gst_object_unref(media_element);

    if (appsrc_element == nullptr || !GST_IS_APP_SRC(appsrc_element)) {
        std::cerr << "RtspRelayServer: relay-source appsrc not found. camera_id="
                  << context->camera_id
                  << std::endl;

        if (appsrc_element != nullptr) {
            gst_object_unref(appsrc_element);
        }
        return;
    }

    auto* media_context = new RelayMediaContext{
        context->relay_hub,
        context->camera_id,
        context->mount_path,
        GST_APP_SRC(appsrc_element),
        false
    };

    g_object_set_data_full(
        G_OBJECT(media),
        kRelayMediaContextKey,
        media_context,
        destroy_relay_media_context
    );

    // prepare에는 live appsrc data가 필요할 수 있으므로 configure 시점에 연결한다.
    attach_relay_media_context(media_context);

    g_signal_connect(media, "prepared", G_CALLBACK(on_media_prepared), media_context);
    g_signal_connect(media, "unprepared", G_CALLBACK(on_media_unprepared), media_context);
}

} // namespace

RtspRelayServer::RtspRelayServer(
    H264RelayHub& relay_hub,
    std::string service,
    std::string bind_address,
    std::string advertised_host,
    bool enabled)
    : relay_hub_(relay_hub),
      service_(std::move(service)),
      bind_address_(std::move(bind_address)),
      advertised_host_(std::move(advertised_host)),
      enabled_(enabled) {}

RtspRelayServer::~RtspRelayServer() {
    stop();
}

bool RtspRelayServer::add_h264_camera(const CameraConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!enabled_) {
        return false;
    }

    if (config.camera_id.empty()) {
        std::cerr << "RtspRelayServer: camera_id is empty" << std::endl;
        return false;
    }

    if (!ensure_server_created_locked()) {
        return false;
    }

    if (mounted_camera_ids_.find(config.camera_id) != mounted_camera_ids_.end()) {
        return true;
    }

    const std::string mount_path = make_mount_path(config.camera_id);
    const std::string output_url = make_public_url(config.camera_id);
    const std::string launch = make_h264_relay_launch();

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
        &relay_hub_,
        config.camera_id,
        mount_path,
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

    mounted_camera_ids_.insert(config.camera_id);

    std::cout << "RtspRelayServer: added H264 relay" << std::endl;
    std::cout << "  camera_id: " << config.camera_id << std::endl;
    std::cout << "  input    : CameraPipeline encoded H264 bridge" << std::endl;
    std::cout << "  output   : " << output_url << std::endl;
    std::cout << "  shared   : true" << std::endl;
    std::cout << "  launch   : " << launch << std::endl;

    return true;
}

bool RtspRelayServer::start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!enabled_) {
        std::cout << "RtspRelayServer disabled" << std::endl;
        return true;
    }

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
    for (const auto& camera_id : mounted_camera_ids_) {
        std::cout << "  " << make_public_url(camera_id) << std::endl;
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

    mounted_camera_ids_.clear();
    running_ = false;

    std::cout << "RtspRelayServer stopped" << std::endl;
}

std::string RtspRelayServer::make_mount_path(const std::string& camera_id) const {
    return "/live/" + camera_id;
}

std::string RtspRelayServer::make_public_url(const std::string& camera_id) const {
    return "rtsp://" + advertised_host_ + ":" + service_ + make_mount_path(camera_id);
}

bool RtspRelayServer::is_enabled() const {
    return enabled_;
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

    g_object_set(
        server_,
        "service", service_.c_str(),
        "address", bind_address_.c_str(),
        nullptr);
    return true;
}

std::string RtspRelayServer::make_h264_relay_launch() const {
    // CameraPipeline의 encoded H264 appsink buffer를 appsrc로 받아 RTP packetize한다.
    // 두 GstPipeline은 분리되어 있지만 decode/re-encode는 하지 않는다.
    return
        "( "
        "appsrc name=relay-source is-live=true format=time "
        "do-timestamp=true block=false "
        "! queue max-size-buffers=60 max-size-bytes=0 "
        "max-size-time=0 leaky=downstream "
        "! h264parse config-interval=-1 "
        "! rtph264pay name=pay0 pt=96 config-interval=1 "
        ")";
}
