#include "HlsVodConverter.h"

#include <gst/gst.h>

#include <filesystem>
#include <iostream>
#include <sstream>
#include <system_error>

namespace {

struct QtdemuxLinkContext {
    GstElement* queue = nullptr;
    bool linked = false;
    std::string error_message;
};

std::string gst_error_to_string(GError* error, gchar* debug_info) {
    std::ostringstream oss;

    if (error != nullptr) {
        oss << error->message;
    } else {
        oss << "unknown GStreamer error";
    }

    if (debug_info != nullptr) {
        oss << " debug=" << debug_info;
    }

    return oss.str();
}

std::string pop_bus_error_message(GstBus* bus) {
    if (bus == nullptr) {
        return "GStreamer bus is null";
    }

    GstMessage* message = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR);
    if (message == nullptr) {
        return "GStreamer state change failed";
    }

    GError* error = nullptr;
    gchar* debug_info = nullptr;
    gst_message_parse_error(message, &error, &debug_info);

    std::string error_message = gst_error_to_string(error, debug_info);

    if (error != nullptr) {
        g_error_free(error);
    }
    if (debug_info != nullptr) {
        g_free(debug_info);
    }

    gst_message_unref(message);
    return error_message;
}

bool caps_is_h264_video(GstCaps* caps) {
    if (caps == nullptr || gst_caps_is_empty(caps)) {
        return false;
    }

    GstStructure* structure = gst_caps_get_structure(caps, 0);
    if (structure == nullptr) {
        return false;
    }

    const char* name = gst_structure_get_name(structure);
    if (name == nullptr) {
        return false;
    }

    return std::string(name) == "video/x-h264";
}

void unref_object(GstObject* object) {
    if (object != nullptr) {
        gst_object_unref(object);
    }
}

gint64 milliseconds_to_nanoseconds(std::int64_t milliseconds) {
    if (milliseconds <= 0) {
        return 0;
    }

    return static_cast<gint64>(milliseconds) * static_cast<gint64>(GST_MSECOND);
}

void on_qtdemux_pad_added(
    GstElement* /*demux*/,
    GstPad* new_pad,
    gpointer user_data
) {
    auto* context = static_cast<QtdemuxLinkContext*>(user_data);

    if (context == nullptr || context->queue == nullptr) {
        return;
    }

    if (context->linked) {
        return;
    }

    GstCaps* caps = gst_pad_get_current_caps(new_pad);
    if (caps == nullptr) {
        caps = gst_pad_query_caps(new_pad, nullptr);
    }

    const bool is_h264 = caps_is_h264_video(caps);
    if (caps != nullptr) {
        gst_caps_unref(caps);
    }

    if (!is_h264) {
        return;
    }

    GstPad* sink_pad = gst_element_get_static_pad(context->queue, "sink");
    if (sink_pad == nullptr) {
        context->error_message = "failed to get queue sink pad";
        return;
    }

    if (gst_pad_is_linked(sink_pad)) {
        context->linked = true;
        gst_object_unref(sink_pad);
        return;
    }

    const GstPadLinkReturn link_result = gst_pad_link(new_pad, sink_pad);
    gst_object_unref(sink_pad);

    if (GST_PAD_LINK_FAILED(link_result)) {
        std::ostringstream oss;
        oss << "failed to link qtdemux h264 pad to queue. code="
            << static_cast<int>(link_result);
        context->error_message = oss.str();
        return;
    }

    context->linked = true;
}

bool wait_until_paused(
    GstElement* pipeline,
    GstBus* bus,
    QtdemuxLinkContext& link_context,
    std::string& error_message
) {
    const GstStateChangeReturn set_result = gst_element_set_state(
        pipeline,
        GST_STATE_PAUSED
    );

    if (set_result == GST_STATE_CHANGE_FAILURE) {
        error_message = pop_bus_error_message(bus);
        return false;
    }

    GstState current_state = GST_STATE_NULL;
    GstState pending_state = GST_STATE_NULL;
    const GstStateChangeReturn wait_result = gst_element_get_state(
        pipeline,
        &current_state,
        &pending_state,
        10 * GST_SECOND
    );

    if (wait_result == GST_STATE_CHANGE_FAILURE) {
        error_message = pop_bus_error_message(bus);
        return false;
    }

    if (wait_result == GST_STATE_CHANGE_ASYNC) {
        error_message = "timed out while waiting for MP4 pipeline preroll";
        return false;
    }

    if (!link_context.error_message.empty()) {
        error_message = link_context.error_message;
        return false;
    }

    if (!link_context.linked) {
        error_message = "qtdemux did not expose a video/x-h264 pad";
        return false;
    }

    return true;
}

bool apply_trim_seek(
    GstElement* pipeline,
    const HlsVodSegment& segment,
    std::string& error_message
) {
    const auto start_ns = milliseconds_to_nanoseconds(segment.source_offset_ms);
    const auto stop_ns = milliseconds_to_nanoseconds(
        segment.source_offset_ms + segment.duration_ms
    );

    if (stop_ns <= start_ns) {
        error_message = "invalid seek range: stop time must be greater than start time";
        return false;
    }

    // 1. Key-unit seek를 사용한다.
    //    H264/H265는 임의 프레임에서 바로 디코딩을 시작하기 어렵기 때문에
    //    실제 시작점은 요청 offset보다 약간 앞의 keyframe이 될 수 있다.
    const auto seek_flags = static_cast<GstSeekFlags>(
        GST_SEEK_FLAG_FLUSH |
        GST_SEEK_FLAG_KEY_UNIT
    );

    const gboolean seek_ok = gst_element_seek(
        pipeline,
        1.0,
        GST_FORMAT_TIME,
        seek_flags,
        GST_SEEK_TYPE_SET,
        start_ns,
        GST_SEEK_TYPE_SET,
        stop_ns
    );

    if (!seek_ok) {
        error_message = "failed to apply GStreamer seek range";
        return false;
    }

    return true;
}

bool run_until_eos_or_error(
    GstElement* pipeline,
    GstBus* bus,
    std::string& error_message
) {
    const GstStateChangeReturn set_result = gst_element_set_state(
        pipeline,
        GST_STATE_PLAYING
    );

    if (set_result == GST_STATE_CHANGE_FAILURE) {
        error_message = pop_bus_error_message(bus);
        return false;
    }

    bool done = false;
    bool ok = false;

    while (!done) {
        GstMessage* message = gst_bus_timed_pop_filtered(
            bus,
            GST_CLOCK_TIME_NONE,
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS)
        );

        if (message == nullptr) {
            continue;
        }

        switch (GST_MESSAGE_TYPE(message)) {
            case GST_MESSAGE_ERROR: {
                GError* error = nullptr;
                gchar* debug_info = nullptr;
                gst_message_parse_error(message, &error, &debug_info);

                error_message = gst_error_to_string(error, debug_info);

                if (error != nullptr) {
                    g_error_free(error);
                }
                if (debug_info != nullptr) {
                    g_free(debug_info);
                }

                done = true;
                ok = false;
                break;
            }

            case GST_MESSAGE_EOS:
                done = true;
                ok = true;
                break;

            default:
                break;
        }

        gst_message_unref(message);
    }

    return ok;
}

} // namespace

HlsVodConvertResult HlsVodConverter::convert_first_segment(
    const HlsVodSession& session
) const {
    if (session.segments.empty()) {
        HlsVodConvertResult result;
        result.ok = false;
        result.error_message = "HLS VOD session has no segment plans";
        return result;
    }

    return convert_segment_to_ts(session.segments.front());
}

HlsVodConvertAllResult HlsVodConverter::convert_all_segments(
    const HlsVodSession& session
) const {
    HlsVodConvertAllResult result;
    result.session_id = session.session_id;
    result.camera_id = session.camera_id;
    result.cache_dir = session.cache_dir;
    result.playlist_path = session.playlist_path;
    result.requested_count = session.segments.size();

    if (session.segments.empty()) {
        result.ok = false;
        result.error_message = "HLS VOD session has no segment plans";
        return result;
    }

    // 1. segment plan을 순차적으로 TS로 변환한다.
    //    37강부터 각 segment의 source_offset_ms/duration_ms를 seek 구간으로 적용한다.
    for (const auto& segment : session.segments) {
        auto segment_result = convert_segment_to_ts(segment);

        if (segment_result.ok) {
            ++result.success_count;
        } else {
            ++result.failure_count;
        }

        result.segment_results.push_back(segment_result);
    }

    result.ok = (result.failure_count == 0);
    if (!result.ok) {
        std::ostringstream oss;
        oss << "failed to convert "
            << result.failure_count
            << " of "
            << result.requested_count
            << " HLS segments";
        result.error_message = oss.str();
    }

    return result;
}

HlsVodConvertResult HlsVodConverter::validate_segment(
    const HlsVodSegment& segment
) const {
    HlsVodConvertResult result;
    result.sequence_no = segment.sequence_no;
    result.source_file_path = segment.source_file_path;
    result.output_file_path = segment.output_file_path;
    result.source_offset_ms = segment.source_offset_ms;
    result.requested_duration_ms = segment.duration_ms;
    result.requested_stop_ms = segment.source_offset_ms + segment.duration_ms;
    result.key_unit_seek = true;

    if (segment.source_file_path.empty()) {
        result.ok = false;
        result.error_message = "source_file_path is empty";
        return result;
    }

    if (segment.output_file_path.empty()) {
        result.ok = false;
        result.error_message = "output_file_path is empty";
        return result;
    }

    if (!std::filesystem::exists(segment.source_file_path)) {
        result.ok = false;
        result.error_message = "source MP4 file does not exist: " + segment.source_file_path;
        return result;
    }

    if (segment.source_offset_ms < 0) {
        result.ok = false;
        result.error_message = "source offset must not be negative";
        return result;
    }

    if (segment.duration_ms <= 0) {
        result.ok = false;
        result.error_message = "segment duration must be positive";
        return result;
    }

    result.ok = true;
    return result;
}

HlsVodConvertResult HlsVodConverter::convert_segment_to_ts(
    const HlsVodSegment& segment
) const {
    // 1. 입력/출력 경로 검증
    auto validation = validate_segment(segment);
    if (!validation.ok) {
        return validation;
    }

    // 2. 출력 디렉터리 생성
    const auto output_parent = segment.output_file_path.parent_path();
    if (!output_parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(output_parent, error);
        if (error) {
            HlsVodConvertResult result = validation;
            result.ok = false;
            result.error_message = "failed to create output directory: " + error.message();
            return result;
        }
    }

    // 3. 기존 output 파일 제거
    //    이전 실행의 stale TS 파일을 성공 결과로 착각하지 않기 위해 먼저 삭제한다.
    {
        std::error_code error;
        std::filesystem::remove(segment.output_file_path, error);
        if (error) {
            HlsVodConvertResult result = validation;
            result.ok = false;
            result.error_message = "failed to remove old output TS file: " + error.message();
            return result;
        }
    }

    // 4. GStreamer element 생성
    GstElement* pipeline = gst_pipeline_new("hls-vod-mp4-to-ts-trim-pipeline");
    GstElement* filesrc = gst_element_factory_make("filesrc", "source");
    GstElement* demux = gst_element_factory_make("qtdemux", "demux");
    GstElement* queue = gst_element_factory_make("queue", "video-queue");
    GstElement* h264parse = gst_element_factory_make("h264parse", "h264-parser");
    GstElement* capsfilter = gst_element_factory_make("capsfilter", "h264-capsfilter");
    GstElement* mux = gst_element_factory_make("mpegtsmux", "ts-muxer");
    GstElement* filesink = gst_element_factory_make("filesink", "sink");

    if (pipeline == nullptr || filesrc == nullptr || demux == nullptr ||
        queue == nullptr || h264parse == nullptr || capsfilter == nullptr ||
        mux == nullptr || filesink == nullptr) {
        unref_object(GST_OBJECT(pipeline));
        unref_object(GST_OBJECT(filesrc));
        unref_object(GST_OBJECT(demux));
        unref_object(GST_OBJECT(queue));
        unref_object(GST_OBJECT(h264parse));
        unref_object(GST_OBJECT(capsfilter));
        unref_object(GST_OBJECT(mux));
        unref_object(GST_OBJECT(filesink));

        HlsVodConvertResult result = validation;
        result.ok = false;
        result.error_message =
            "failed to create one or more GStreamer elements. "
            "Required elements: filesrc, qtdemux, queue, h264parse, capsfilter, mpegtsmux, filesink";
        return result;
    }

    // 5. element property 설정
    g_object_set(filesrc, "location", segment.source_file_path.c_str(), nullptr);
    g_object_set(filesink, "location", segment.output_file_path.string().c_str(), nullptr);
    g_object_set(filesink, "sync", FALSE, nullptr);
    g_object_set(filesink, "async", FALSE, nullptr);

    // MPEG-TS 안의 H264는 byte-stream/AU 정렬 형태가 다루기 쉽다.
    g_object_set(h264parse, "config-interval", -1, nullptr);

    GstCaps* h264_caps = gst_caps_from_string(
        "video/x-h264,stream-format=byte-stream,alignment=au"
    );
    g_object_set(capsfilter, "caps", h264_caps, nullptr);
    gst_caps_unref(h264_caps);

    gst_bin_add_many(
        GST_BIN(pipeline),
        filesrc,
        demux,
        queue,
        h264parse,
        capsfilter,
        mux,
        filesink,
        nullptr
    );

    if (!gst_element_link(filesrc, demux)) {
        gst_object_unref(pipeline);

        HlsVodConvertResult result = validation;
        result.ok = false;
        result.error_message = "failed to link filesrc -> qtdemux";
        return result;
    }

    if (!gst_element_link_many(queue, h264parse, capsfilter, mux, filesink, nullptr)) {
        gst_object_unref(pipeline);

        HlsVodConvertResult result = validation;
        result.ok = false;
        result.error_message = "failed to link queue -> h264parse -> capsfilter -> mpegtsmux -> filesink";
        return result;
    }

    // qtdemux의 video pad는 동적으로 생성되므로 pad-added에서 queue와 연결한다.
    QtdemuxLinkContext link_context;
    link_context.queue = queue;
    g_signal_connect(demux, "pad-added", G_CALLBACK(on_qtdemux_pad_added), &link_context);

    GstBus* bus = gst_element_get_bus(pipeline);

    // 6. PAUSED까지 올려 demux pad를 만들고, 그 다음 seek 범위를 적용한다.
    std::string error_message;
    if (!wait_until_paused(pipeline, bus, link_context, error_message)) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(bus);
        gst_object_unref(pipeline);

        HlsVodConvertResult result = validation;
        result.ok = false;
        result.error_message = error_message;
        return result;
    }

    if (!apply_trim_seek(pipeline, segment, error_message)) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(bus);
        gst_object_unref(pipeline);

        HlsVodConvertResult result = validation;
        result.ok = false;
        result.error_message = error_message;
        return result;
    }

    // 7. seek가 적용된 구간만 PLAYING으로 흘려 EOS까지 변환한다.
    const bool ok = run_until_eos_or_error(pipeline, bus, error_message);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(pipeline);

    if (!ok) {
        HlsVodConvertResult result = validation;
        result.ok = false;
        result.error_message = error_message.empty()
            ? "GStreamer conversion failed"
            : error_message;
        return result;
    }

    if (!std::filesystem::exists(segment.output_file_path)) {
        HlsVodConvertResult result = validation;
        result.ok = false;
        result.error_message = "conversion finished but output TS file was not created";
        return result;
    }

    HlsVodConvertResult result = validation;
    result.ok = true;
    result.trim_applied = true;
    result.key_unit_seek = true;
    return result;
}

void HlsVodConverter::print_convert_result(
    const HlsVodConvertResult& result
) {
    std::cout << std::endl;
    std::cout << "===== HlsVodConvertResult =====" << std::endl;
    std::cout << "ok=" << (result.ok ? "true" : "false") << std::endl;
    std::cout << "sequence_no=" << result.sequence_no << std::endl;
    std::cout << "source_file_path=" << result.source_file_path.string() << std::endl;
    std::cout << "output_file_path=" << result.output_file_path.string() << std::endl;
    std::cout << "source_offset_ms=" << result.source_offset_ms << std::endl;
    std::cout << "requested_duration_ms=" << result.requested_duration_ms << std::endl;
    std::cout << "requested_stop_ms=" << result.requested_stop_ms << std::endl;
    std::cout << "trim_applied=" << (result.trim_applied ? "true" : "false") << std::endl;
    std::cout << "key_unit_seek=" << (result.key_unit_seek ? "true" : "false") << std::endl;

    if (!result.ok) {
        std::cout << "error=" << result.error_message << std::endl;
    }

    std::cout << "================================" << std::endl;
}

void HlsVodConverter::print_convert_all_result(
    const HlsVodConvertAllResult& result
) {
    std::cout << std::endl;
    std::cout << "===== HlsVodConvertAllResult =====" << std::endl;
    std::cout << "ok=" << (result.ok ? "true" : "false") << std::endl;
    std::cout << "session_id=" << result.session_id << std::endl;
    std::cout << "camera_id=" << result.camera_id << std::endl;
    std::cout << "cache_dir=" << result.cache_dir.string() << std::endl;
    std::cout << "playlist_path=" << result.playlist_path.string() << std::endl;
    std::cout << "requested_count=" << result.requested_count << std::endl;
    std::cout << "success_count=" << result.success_count << std::endl;
    std::cout << "failure_count=" << result.failure_count << std::endl;

    if (!result.ok) {
        std::cout << "error=" << result.error_message << std::endl;
    }

    for (const auto& segment_result : result.segment_results) {
        std::cout << std::endl;
        std::cout << "  [Segment Convert Result "
                  << segment_result.sequence_no
                  << "]"
                  << std::endl;
        std::cout << "    ok=" << (segment_result.ok ? "true" : "false") << std::endl;
        std::cout << "    source_file_path=" << segment_result.source_file_path.string() << std::endl;
        std::cout << "    output_file_path=" << segment_result.output_file_path.string() << std::endl;
        std::cout << "    source_offset_ms=" << segment_result.source_offset_ms << std::endl;
        std::cout << "    requested_duration_ms=" << segment_result.requested_duration_ms << std::endl;
        std::cout << "    requested_stop_ms=" << segment_result.requested_stop_ms << std::endl;
        std::cout << "    trim_applied=" << (segment_result.trim_applied ? "true" : "false") << std::endl;
        std::cout << "    key_unit_seek=" << (segment_result.key_unit_seek ? "true" : "false") << std::endl;

        if (!segment_result.ok) {
            std::cout << "    error=" << segment_result.error_message << std::endl;
        }
    }

    std::cout << "==================================" << std::endl;
}
