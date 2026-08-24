#include "CameraPipeline.h"

#include "H264RelayHub.h"

#include <iostream>
#include <utility>

namespace {

void unref_element_if_needed(GstElement*& element) {
    if (element != nullptr) {
        gst_object_unref(element);
        element = nullptr;
    }
}

} // namespace

CameraPipeline::CameraPipeline(
    std::int64_t profile_id,
    std::string camera_id,
    std::string rtsp_uri,
    std::string record_pattern,
    int split_seconds,
    RecordingSegmentStore* segment_store,
    H264RelayHub* relay_hub
)
    : profile_id_(profile_id),
      camera_id_(std::move(camera_id)),
      rtsp_uri_(std::move(rtsp_uri)),
      record_pattern_(std::move(record_pattern)),
      split_seconds_(split_seconds),
      segment_store_(segment_store),
      relay_hub_(relay_hub) {}

CameraPipeline::~CameraPipeline() {
    stop();
}

bool CameraPipeline::create() {
    // 1. 기존 파이프라인 정리
    stop();

    // 2. 파이프라인 생성
    pipeline_ = gst_pipeline_new("camera-record-relay-feed-pipeline");

    if (pipeline_ == nullptr) {
        std::cerr << "Failed to create pipeline" << std::endl;
        return false;
    }

    // 3. Element 생성
    if (!create_elements()) {
        stop();
        return false;
    }

    // 4. 파이프라인에 Element 추가
    if (!add_elements_to_pipeline()) {
        stop();
        return false;
    }

    // 5. Element 속성 설정
    if (!configure_elements()) {
        stop();
        return false;
    }

    // 6. 정적 구간 연결
    if (!link_static_elements()) {
        stop();
        return false;
    }

    // 7. tee branch 연결
    if (!link_tee_branches()) {
        stop();
        return false;
    }

    // 8. Dynamic Pad 콜백 등록
    g_signal_connect(
        source_,
        "pad-added",
        G_CALLBACK(&CameraPipeline::on_pad_added),
        this
    );

    // 9. encoded H.264 relay appsink 콜백 등록
    if (relay_sink_ != nullptr) {
        g_signal_connect(
            relay_sink_,
            "new-sample",
            G_CALLBACK(&CameraPipeline::on_new_sample),
            this
        );
    }

    // 10. splitmuxsink 파일명 생성 콜백 등록
    g_signal_connect(
        split_sink_,
        "format-location",
        G_CALLBACK(&CameraPipeline::on_format_location),
        this
    );

    return true;
}

bool CameraPipeline::create_elements() {
    // 1. 공통 Element 생성
    source_ = gst_element_factory_make("rtspsrc", "source");
    depay_ = gst_element_factory_make("rtph264depay", "depay");
    parser_ = gst_element_factory_make("h264parse", "parser");
    tee_ = gst_element_factory_make("tee", "tee");

    // 2. 녹화 branch Element 생성
    record_queue_ = gst_element_factory_make("queue", "record-queue");
    split_sink_ = gst_element_factory_make("splitmuxsink", "split-sink");

    // 3. relay가 활성화된 경우 encoded H.264 appsink branch Element 생성
    if (relay_hub_ != nullptr) {
        relay_queue_ = gst_element_factory_make("queue", "relay-feed-queue");
        relay_sink_ = gst_element_factory_make("appsink", "relay-h264-sink");
    }

    // 4. Element 생성 확인
    if (source_ == nullptr || depay_ == nullptr || parser_ == nullptr ||
        tee_ == nullptr || record_queue_ == nullptr || split_sink_ == nullptr ||
        (relay_hub_ != nullptr &&
         (relay_queue_ == nullptr || relay_sink_ == nullptr))) {
        std::cerr << "Failed to create one or more GStreamer elements" << std::endl;

        unref_element_if_needed(source_);
        unref_element_if_needed(depay_);
        unref_element_if_needed(parser_);
        unref_element_if_needed(tee_);
        unref_element_if_needed(record_queue_);
        unref_element_if_needed(split_sink_);
        unref_element_if_needed(relay_queue_);
        unref_element_if_needed(relay_sink_);

        return false;
    }

    return true;
}

bool CameraPipeline::add_elements_to_pipeline() {
    // 1. 파이프라인에 Element 추가
    gst_bin_add_many(
        GST_BIN(pipeline_),
        source_,
        depay_,
        parser_,
        tee_,
        record_queue_,
        split_sink_,
        nullptr
    );

    if (relay_hub_ != nullptr) {
        gst_bin_add_many(
            GST_BIN(pipeline_),
            relay_queue_,
            relay_sink_,
            nullptr
        );
    }

    return true;
}

bool CameraPipeline::configure_elements() {
    // 1. rtspsrc 설정
    g_object_set(
        source_,
        "location", rtsp_uri_.c_str(),
        "latency", 100,
        nullptr
    );

    // 2. h264parse 설정
    g_object_set(
        parser_,
        "config-interval", -1,
        nullptr
    );

    // 3. splitmuxsink 설정
    g_object_set(
        split_sink_,
        "location", record_pattern_.c_str(),
        "max-size-time", static_cast<guint64>(split_seconds_) * GST_SECOND,
        nullptr
    );

    // 4. relay branch는 녹화를 막지 않도록 bounded/leaky queue를 사용한다.
    if (relay_hub_ != nullptr) {
        g_object_set(
            relay_queue_,
            "max-size-buffers", 60,
            "max-size-bytes", 0,
            "max-size-time", static_cast<guint64>(0),
            "leaky", 2,
            nullptr
        );

        g_object_set(
            relay_sink_,
            "emit-signals", TRUE,
            "sync", FALSE,
            "max-buffers", 1,
            "drop", TRUE,
            nullptr
        );
    }

    return true;
}

bool CameraPipeline::link_static_elements() {
    // 1. depay → parser → tee 연결
    if (!gst_element_link_many(depay_, parser_, tee_, nullptr)) {
        std::cerr << "Failed to link depay, parser, tee" << std::endl;
        return false;
    }

    // 2. 녹화 branch 내부 연결
    if (!gst_element_link(record_queue_, split_sink_)) {
        std::cerr << "Failed to link record_queue and split_sink" << std::endl;
        return false;
    }

    // 3. encoded H.264 relay branch 내부 연결
    if (relay_hub_ != nullptr) {
        if (!gst_element_link(relay_queue_, relay_sink_)) {
            std::cerr << "Failed to link H264 relay feed branch" << std::endl;
            return false;
        }
    }

    return true;
}

bool CameraPipeline::link_tee_branches() {
    // 1. tee request pad 생성
    tee_record_pad_ = gst_element_request_pad_simple(tee_, "src_%u");
    if (relay_hub_ != nullptr) {
        tee_relay_pad_ = gst_element_request_pad_simple(tee_, "src_%u");
    }

    // 2. queue sink pad 가져오기
    record_queue_sink_pad_ = gst_element_get_static_pad(record_queue_, "sink");
    if (relay_hub_ != nullptr) {
        relay_queue_sink_pad_ = gst_element_get_static_pad(relay_queue_, "sink");
    }

    if (tee_record_pad_ == nullptr || record_queue_sink_pad_ == nullptr ||
        (relay_hub_ != nullptr &&
         (tee_relay_pad_ == nullptr || relay_queue_sink_pad_ == nullptr))) {
        std::cerr << "Failed to get tee or queue pads" << std::endl;
        return false;
    }

    // 3. tee → record queue 연결
    if (gst_pad_link(tee_record_pad_, record_queue_sink_pad_) != GST_PAD_LINK_OK) {
        std::cerr << "Failed to link tee to record queue" << std::endl;
        return false;
    }

    // 4. tee → encoded H.264 relay queue 연결
    if (relay_hub_ != nullptr) {
        if (gst_pad_link(tee_relay_pad_, relay_queue_sink_pad_) != GST_PAD_LINK_OK) {
            std::cerr << "Failed to link tee to H264 relay queue" << std::endl;
            return false;
        }
    }

    return true;
}

bool CameraPipeline::start() {
    // 1. 파이프라인 생성 여부 확인
    if (pipeline_ == nullptr) {
        std::cerr << "Pipeline is not created" << std::endl;
        return false;
    }

    // 2. PLAYING 상태로 전환
    GstStateChangeReturn ret = gst_element_set_state(
        pipeline_,
        GST_STATE_PLAYING
    );

    // 3. 실행 상태 확인
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "Failed to set pipeline to PLAYING" << std::endl;
        return false;
    }

    return true;
}

CameraPipelineEvent CameraPipeline::poll_bus(int timeout_ms) {
    // 1. 파이프라인 생성 여부 확인
    if (pipeline_ == nullptr) {
        return CameraPipelineEvent::Error;
    }

    // 2. Bus 가져오기
    GstBus* bus = gst_element_get_bus(pipeline_);

    if (bus == nullptr) {
        return CameraPipelineEvent::Error;
    }

    // 3. 지정 시간 동안 메시지 확인
    GstMessage* msg = gst_bus_timed_pop_filtered(
        bus,
        static_cast<GstClockTime>(timeout_ms) * GST_MSECOND,
        static_cast<GstMessageType>(
            GST_MESSAGE_ERROR |
            GST_MESSAGE_EOS |
            GST_MESSAGE_ELEMENT
        )
    );

    CameraPipelineEvent event = CameraPipelineEvent::None;

    // 4. 메시지 처리
    if (msg != nullptr) {
        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR:
            handle_bus_message(msg);
            event = CameraPipelineEvent::Error;
            break;

        case GST_MESSAGE_EOS:
            handle_bus_message(msg);
            event = CameraPipelineEvent::Eos;
            break;

        case GST_MESSAGE_ELEMENT:
            handle_bus_message(msg);
            event = CameraPipelineEvent::None;
            break;

        default:
            event = CameraPipelineEvent::None;
            break;
        }

        gst_message_unref(msg);
    }

    // 5. Bus 해제
    gst_object_unref(bus);

    return event;
}

void CameraPipeline::stop() {
    // 1. 파이프라인 존재 여부 확인
    if (pipeline_ == nullptr) {
        if (relay_hub_ != nullptr) {
            relay_hub_->reset_source(camera_id_);
        }

        source_ = nullptr;
        depay_ = nullptr;
        parser_ = nullptr;
        tee_ = nullptr;

        record_queue_ = nullptr;
        split_sink_ = nullptr;

        relay_queue_ = nullptr;
        relay_sink_ = nullptr;

        tee_record_pad_ = nullptr;
        tee_relay_pad_ = nullptr;
        record_queue_sink_pad_ = nullptr;
        relay_queue_sink_pad_ = nullptr;

        return;
    }

    // 2. EOS로 마지막 파일 마무리 시도
    bool finalized = finalize_recording(3000);

    // 3. EOS로 마무리하지 못한 recording segment는 interrupted 처리
    if (!finalized && segment_store_ != nullptr) {
        segment_store_->mark_open_segments_interrupted(camera_id_);
    }

    // 4. 파이프라인 정지
    gst_element_set_state(pipeline_, GST_STATE_NULL);

    // 5. tee request pad 해제
    if (tee_ != nullptr && tee_record_pad_ != nullptr) {
        gst_element_release_request_pad(tee_, tee_record_pad_);
        gst_object_unref(tee_record_pad_);
        tee_record_pad_ = nullptr;
    }

    if (tee_ != nullptr && tee_relay_pad_ != nullptr) {
        gst_element_release_request_pad(tee_, tee_relay_pad_);
        gst_object_unref(tee_relay_pad_);
        tee_relay_pad_ = nullptr;
    }

    // 6. queue sink pad 참조 해제
    if (record_queue_sink_pad_ != nullptr) {
        gst_object_unref(record_queue_sink_pad_);
        record_queue_sink_pad_ = nullptr;
    }

    if (relay_queue_sink_pad_ != nullptr) {
        gst_object_unref(relay_queue_sink_pad_);
        relay_queue_sink_pad_ = nullptr;
    }

    // 7. 파이프라인 해제
    gst_object_unref(pipeline_);

    pipeline_ = nullptr;

    source_ = nullptr;
    depay_ = nullptr;
    parser_ = nullptr;
    tee_ = nullptr;

    record_queue_ = nullptr;
    split_sink_ = nullptr;

    relay_queue_ = nullptr;
    relay_sink_ = nullptr;

    if (relay_hub_ != nullptr) {
        relay_hub_->reset_source(camera_id_);
    }
}

void CameraPipeline::on_pad_added(
    GstElement* src,
    GstPad* new_pad,
    gpointer user_data
) {
    (void)src;

    CameraPipeline* self = static_cast<CameraPipeline*>(user_data);

    if (self == nullptr) {
        return;
    }

    self->handle_pad_added(new_pad);
}

void CameraPipeline::handle_pad_added(GstPad* new_pad) {
    // 1. rtph264depay의 sink pad 가져오기
    GstPad* sink_pad = gst_element_get_static_pad(depay_, "sink");

    if (sink_pad == nullptr) {
        std::cerr << "Failed to get depay sink pad" << std::endl;
        return;
    }

    // 2. 이미 연결되어 있으면 무시
    if (gst_pad_is_linked(sink_pad)) {
        gst_object_unref(sink_pad);
        return;
    }

    // 3. 새 pad의 caps 확인
    GstCaps* caps = gst_pad_get_current_caps(new_pad);

    if (caps == nullptr) {
        caps = gst_pad_query_caps(new_pad, nullptr);
    }

    if (caps == nullptr || gst_caps_get_size(caps) == 0) {
        std::cerr << "Failed to get new pad caps" << std::endl;

        if (caps != nullptr) {
            gst_caps_unref(caps);
        }

        gst_object_unref(sink_pad);
        return;
    }

    // 4. RTP pad인지 확인
    GstStructure* structure = gst_caps_get_structure(caps, 0);
    const gchar* pad_type = gst_structure_get_name(structure);

    if (g_str_has_prefix(pad_type, "application/x-rtp") == FALSE) {
        gst_caps_unref(caps);
        gst_object_unref(sink_pad);
        return;
    }

    // 5. H264 RTP인지 확인
    const gchar* encoding_name = gst_structure_get_string(
        structure,
        "encoding-name"
    );

    std::cout << "RTP encoding-name: "
              << (encoding_name ? encoding_name : "none")
              << std::endl;

    if (encoding_name == nullptr ||
        g_ascii_strcasecmp(encoding_name, "H264") != 0) {
        std::cout << "It is not H264 RTP. Ignore this pad." << std::endl;

        gst_caps_unref(caps);
        gst_object_unref(sink_pad);
        return;
    }

    // 6. rtspsrc의 동적 pad를 rtph264depay에 연결
    GstPadLinkReturn ret = gst_pad_link(new_pad, sink_pad);

    if (GST_PAD_LINK_FAILED(ret)) {
        std::cerr << "Failed to link rtspsrc dynamic pad" << std::endl;
    } else {
        std::cout << "RTSP dynamic pad linked successfully" << std::endl;
    }

    // 7. 참조 해제
    gst_caps_unref(caps);
    gst_object_unref(sink_pad);
}

GstFlowReturn CameraPipeline::handle_new_sample(GstAppSink* appsink) {
    // 1. encoded H.264 appsink에서 sample 가져오기
    GstSample* sample = gst_app_sink_pull_sample(appsink);

    if (sample == nullptr) {
        return GST_FLOW_ERROR;
    }

    // 2. relay pipeline이 없거나 잠시 중단돼도 recording 흐름은 유지한다.
    if (relay_hub_ != nullptr) {
        relay_hub_->publish_sample(camera_id_, sample);
    }

    // 3. sample 해제
    gst_sample_unref(sample);

    return GST_FLOW_OK;
}

gchar* CameraPipeline::handle_format_location(guint fragment_id) {
    // 1. 파일명 생성
    gchar* path = g_strdup_printf(
        record_pattern_.c_str(),
        fragment_id
    );

    if (path == nullptr) { return nullptr; }

    // 2. Segment 시작 기록
    if (segment_store_ != nullptr) {
        segment_store_->on_segment_started(
            profile_id_,
            camera_id_,
            path
        );
    }

    // 3. splitmuxsink가 나중에 g_free()  할 수 있도록 반환
    return path;
}

bool CameraPipeline::finalize_recording(int timeout_ms) {
    // 1. 파이프라인 존재 여부 확인
    if (pipeline_ == nullptr) { return false; }

    // 2. Bus 가져오기
    GstBus* bus = gst_element_get_bus(pipeline_);

    if (bus == nullptr) { return false; }

    // 3. EOS 전송
    std::cout << "[" << camera_id_ << "] Sending EOS to finalize recording..."
              << std::endl;

    gboolean sent = gst_element_send_event(
        pipeline_,
        gst_event_new_eos()
    );

    if (!sent) {
        std::cerr << "[" << camera_id_ << "] Failed to send EOS" << std::endl;
        gst_object_unref(bus);
        return false;
    }

    // 4. timeout까지 bus 메시지 처리
    gint64 start_time = g_get_monotonic_time();
    gint64 timeout_us = static_cast<gint64>(timeout_ms) * 1000;

    bool eos_received = false;

    while (true) {
        gint64 now = g_get_monotonic_time();
        gint64 elapsed = now - start_time;

        if (elapsed >= timeout_us) { break; }

        gint64 remain_us = timeout_us - elapsed;

        GstMessage* msg = gst_bus_timed_pop_filtered(
            bus,
            static_cast<GstClockType>(remain_us) * GST_USECOND,
            static_cast<GstMessageType>(
                GST_MESSAGE_ERROR |
                GST_MESSAGE_EOS |
                GST_MESSAGE_ELEMENT
            )
        );

        if (msg == nullptr) { break; }

        GstMessageType type = GST_MESSAGE_TYPE(msg);

        handle_bus_message(msg);

        if (type == GST_MESSAGE_ERROR) {
            gst_message_unref(msg);
            break;
        }

        if (type == GST_MESSAGE_EOS) {
            eos_received = true;
            gst_message_unref(msg);
            break;
        }

        gst_message_unref(msg);
    }
    
    // 5. Bus 해제
    gst_object_unref(bus);

    if (eos_received) {
        std::cout << "[" << camera_id_ << "] Recordding finalize by EOS"
                  << std::endl;
    } else {
        std::cout << "[" << camera_id_ << "] EOS finalize timeout or failed"
                  << std::endl;
    }

    return eos_received;
}

void CameraPipeline::handle_bus_message(GstMessage* msg) {
    if (msg == nullptr) { return; }

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError* err = nullptr;
        gchar* debug_info = nullptr;

        gst_message_parse_error(msg, &err, &debug_info);

        std::cerr << "GStreamer ERROR: "
                  << (err ? err->message : "unknown error")
                  << std::endl;

        std::cerr << "Debug info: "
                  << (debug_info ? debug_info : "none")
                  << std::endl;

        if (err != nullptr) {
            g_error_free(err);
        }

        if (debug_info != nullptr) {
            g_free(debug_info);
        }

        break;
    }

    case GST_MESSAGE_EOS:
        std::cout << "End of stream" << std::endl;
        break;

    case GST_MESSAGE_ELEMENT:
        handle_element_message(msg);
        break;

    default:
        break;
    }
}

void CameraPipeline::handle_element_message(GstMessage* msg) {
    // 1. 메시지 구조 확인
    const GstStructure* structure = gst_message_get_structure(msg);

    if (structure == nullptr) {
        return;
    }

    const gchar* name = gst_structure_get_name(structure);

    if (name == nullptr) {
        return;
    }

    // 2. splitmuxsink fragment closed 메시지만 처리
    if (g_strcmp0(name, "splitmuxsink-fragment-closed") != 0) {
        return;
    }

    // 3. location 필드 확인
    const gchar* location = gst_structure_get_string(
        structure,
        "location"
    );

    if (location == nullptr) {
        std::cout << "[SEGMENT FINISHED] "
                  << "camera_id=" << camera_id_
                  << ", warning=location field not found"
                  << std::endl;
        return;
    }

    // 4. Segment 종료 기록
    if (segment_store_ != nullptr) {
        segment_store_->on_segment_finished(
            camera_id_,
            location
        );
    }
}

GstFlowReturn CameraPipeline::on_new_sample(
    GstAppSink* appsink,
    gpointer user_data
) {
    CameraPipeline* self = static_cast<CameraPipeline*>(user_data);

    if (self == nullptr) {
        return GST_FLOW_ERROR;
    }

    return self->handle_new_sample(appsink);
}

gchar* CameraPipeline::on_format_location(
    GstElement* splitmux,
    guint fragment_id,
    gpointer user_data
) {
    (void)splitmux;

    CameraPipeline* self = static_cast<CameraPipeline*>(user_data);

    if (self == nullptr) { return nullptr; }

    return self->handle_format_location(fragment_id);
}
