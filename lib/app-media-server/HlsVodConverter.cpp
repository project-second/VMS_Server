#include "HlsVodConverter.h"

#include <gst/gst.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <system_error>

namespace {

struct QtdemuxLinkContext {
    GstElement* queue = nullptr;
    bool linked = false;
    std::string error_message;
};

struct RawClipContext {
    GstClockTime start = 0;
    GstClockTime stop = 0;
    GstSegment input_segment;
    GstClockTime frame_duration = GST_CLOCK_TIME_NONE;
    std::size_t decoded_frames = 0;
    GstClockTime source_video_end = 0;
    bool has_uncertain_timing = false;
    std::size_t kept_frames = 0;
};

struct OutputTimeline {
    GstSegment segment;
    GstClockTime first = GST_CLOCK_TIME_NONE;
    GstClockTime end = 0;
    GstClockTime frame_duration = GST_CLOCK_TIME_NONE;
    // DTS가 PTS보다 앞서는 영상에도 음수가 생기지 않도록 여유를 둔다.
    GstClockTime output_base = 10 * GST_SECOND;
    bool invalid_timestamp = false;
    std::string sps;
    std::string pps;
};

void collect_parameter_sets(GstBuffer* buffer, OutputTimeline& timeline) {
    if (!timeline.sps.empty() && !timeline.pps.empty()) return;
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) return;
    const auto start_code_size = [&map](std::size_t index) -> std::size_t {
        if (index + 3 <= map.size && map.data[index] == 0 && map.data[index + 1] == 0) {
            if (map.data[index + 2] == 1) return 3;
            if (index + 4 <= map.size && map.data[index + 2] == 0 && map.data[index + 3] == 1) return 4;
        }
        return 0;
    };
    for (std::size_t index = 0; index + 3 < map.size;) {
        const auto prefix = start_code_size(index);
        if (prefix == 0) { ++index; continue; }
        const auto start = index + prefix;
        if (start >= map.size) break;
        auto end = start + 1;
        while (end < map.size && start_code_size(end) == 0) ++end;
        const auto type = map.data[start] & 0x1f;
        if (type == 7 && timeline.sps.empty()) {
            timeline.sps.assign(reinterpret_cast<const char*>(map.data + start), end - start);
        } else if (type == 8 && timeline.pps.empty()) {
            timeline.pps.assign(reinterpret_cast<const char*>(map.data + start), end - start);
        }
        index = end;
    }
    gst_buffer_unmap(buffer, &map);
}

// 파일 시각이나 DB 종료 콜백 대신 mux에 전달한 실제 표시 구간을 측정한다.
GstPadProbeReturn measure_output_timeline(
    GstPad* /*pad*/, GstPadProbeInfo* info, gpointer user_data
) {
    auto& timeline = *static_cast<OutputTimeline*>(user_data);
    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
        GstEvent* event = GST_PAD_PROBE_INFO_EVENT(info);
        if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT) {
            const GstSegment* segment = nullptr;
            gst_event_parse_segment(event, &segment);
            timeline.segment = *segment;
            // remux의 preroll 뒤 flushing seek가 새 표시 구간을 시작한다.
            timeline.first = GST_CLOCK_TIME_NONE;
            timeline.end = 0;
            timeline.invalid_timestamp = false;
            GstSegment output;
            gst_segment_init(&output, GST_FORMAT_TIME);
            GstEvent* replacement = gst_event_new_segment(&output);
            gst_event_set_seqnum(replacement, gst_event_get_seqnum(event));
            gst_event_unref(event);
            GST_PAD_PROBE_INFO_DATA(info) = replacement;
        } else if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
            GstCaps* caps = nullptr;
            gst_event_parse_caps(event, &caps);
            int numerator = 0;
            int denominator = 0;
            if (caps != nullptr && !gst_caps_is_empty(caps) && !gst_caps_is_any(caps) &&
                gst_structure_get_fraction(gst_caps_get_structure(caps, 0),
                    "framerate", &numerator, &denominator) &&
                numerator > 0 && denominator > 0) {
                timeline.frame_duration = gst_util_uint64_scale(
                    GST_SECOND, denominator, numerator);
            }
        }
        return GST_PAD_PROBE_OK;
    }

    GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (buffer == nullptr) return GST_PAD_PROBE_OK;
    collect_parameter_sets(buffer, timeline);
    if (!GST_BUFFER_PTS_IS_VALID(buffer) || timeline.segment.format != GST_FORMAT_TIME) {
        if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_HEADER)) return GST_PAD_PROBE_OK;
        timeline.invalid_timestamp = true;
        return GST_PAD_PROBE_DROP;
    }
    const auto pts = gst_segment_to_running_time(
        &timeline.segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer));
    // seek 경계 바깥의 packet은 새 mux 시간축으로 보내지 않는다.
    if (!GST_CLOCK_TIME_IS_VALID(pts)) return GST_PAD_PROBE_DROP;
    const auto duration = GST_BUFFER_DURATION_IS_VALID(buffer) && GST_BUFFER_DURATION(buffer) > 0
        ? GST_BUFFER_DURATION(buffer) : timeline.frame_duration;
    if (!GST_CLOCK_TIME_IS_VALID(duration) ||
        duration == 0 || duration >= GST_CLOCK_TIME_NONE - pts) {
        timeline.invalid_timestamp = true;
        return GST_PAD_PROBE_DROP;
    }
    if (!GST_CLOCK_TIME_IS_VALID(timeline.first) || pts < timeline.first) timeline.first = pts;
    timeline.end = std::max(timeline.end, pts + duration);

    GstClockTime dts = GST_CLOCK_TIME_NONE;
    if (GST_BUFFER_DTS_IS_VALID(buffer)) {
        guint64 running_dts = 0;
        GstSegment decoding_segment = timeline.segment;
        decoding_segment.stop = GST_CLOCK_TIME_NONE;
        const auto sign = gst_segment_to_running_time_full(
            &decoding_segment, GST_FORMAT_TIME, GST_BUFFER_DTS(buffer), &running_dts);
        if (sign == 0 || (sign < 0 && running_dts > timeline.output_base)) {
            timeline.invalid_timestamp = true;
            return GST_PAD_PROBE_DROP;
        }
        dts = sign < 0 ? timeline.output_base - running_dts : timeline.output_base + running_dts;
    }
    buffer = gst_buffer_make_writable(buffer);
    GST_PAD_PROBE_INFO_DATA(info) = buffer;
    GST_BUFFER_PTS(buffer) = timeline.output_base + pts;
    GST_BUFFER_DTS(buffer) = dts;
    return GST_PAD_PROBE_OK;
}

bool continue_ts_counters(
    const std::filesystem::path& path, std::array<int, 8192>& counters, std::string& error
) {
    std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!file) { error = "failed to open output TS for continuity counters"; return false; }
    std::array<unsigned char, 188> packet;
    for (std::streamoff offset = 0;; offset += packet.size()) {
        file.seekg(offset);
        file.read(reinterpret_cast<char*>(packet.data()), packet.size());
        if (file.gcount() == 0 && file.eof()) return true;
        if (file.gcount() != static_cast<std::streamsize>(packet.size()) || packet[0] != 0x47) {
            error = "invalid output MPEG-TS packet";
            return false;
        }
        const auto pid = ((packet[1] & 0x1f) << 8) | packet[2];
        if (pid == 0x1fff) continue;
        auto& counter = counters[pid];
        if (counter < 0) counter = packet[3] & 0x0f;
        else if (packet[3] & 0x10) counter = (counter + 1) & 0x0f;
        packet[3] = static_cast<unsigned char>((packet[3] & 0xf0) | counter);
        file.seekp(offset + 3);
        file.put(static_cast<char>(packet[3]));
        if (!file) { error = "failed to write output TS continuity counter"; return false; }
    }
}

// 압축 스트림의 sync sample이 IDR을 보장하지 않으므로, 디코더에는 원본을
// 처음부터 전달하고 인코더 앞의 raw frame만 요청 구간으로 자른다.
GstPadProbeReturn clip_decoded_video(
    GstPad* /*pad*/, GstPadProbeInfo* info, gpointer user_data
) {
    auto& clip = *static_cast<RawClipContext*>(user_data);
    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
        GstEvent* event = GST_PAD_PROBE_INFO_EVENT(info);
        if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT) {
            const GstSegment* input = nullptr;
            gst_event_parse_segment(event, &input);
            if (input->format == GST_FORMAT_TIME) {
                clip.input_segment = *input;
                GstSegment output;
                gst_segment_init(&output, GST_FORMAT_TIME);
                output.stop = clip.stop - clip.start;
                output.duration = output.stop;
                GstEvent* replacement = gst_event_new_segment(&output);
                gst_event_set_seqnum(replacement, gst_event_get_seqnum(event));
                gst_event_unref(event);
                GST_PAD_PROBE_INFO_DATA(info) = replacement;
            }
        } else if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
            GstCaps* caps = nullptr;
            gst_event_parse_caps(event, &caps);
            int numerator = 0;
            int denominator = 0;
            if (caps != nullptr && !gst_caps_is_empty(caps) && !gst_caps_is_any(caps) &&
                gst_structure_get_fraction(gst_caps_get_structure(caps, 0),
                    "framerate", &numerator, &denominator) &&
                numerator > 0 && denominator > 0) {
                clip.frame_duration = gst_util_uint64_scale(
                    GST_SECOND, denominator, numerator);
            }
        }
        return GST_PAD_PROBE_OK;
    }

    GstBuffer* buffer = GST_PAD_PROBE_INFO_BUFFER(info);
    if (buffer == nullptr || !GST_BUFFER_PTS_IS_VALID(buffer)) {
        clip.has_uncertain_timing = true;
        return GST_PAD_PROBE_DROP;
    }
    const auto pts = gst_segment_to_stream_time(
        &clip.input_segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer));
    if (!GST_CLOCK_TIME_IS_VALID(pts)) {
        clip.has_uncertain_timing = true;
        return GST_PAD_PROBE_DROP;
    }

    const auto duration = GST_BUFFER_DURATION_IS_VALID(buffer)
        ? GST_BUFFER_DURATION(buffer) : clip.frame_duration;
    ++clip.decoded_frames;
    const auto frame_end = GST_CLOCK_TIME_IS_VALID(duration) && duration < GST_CLOCK_TIME_NONE - pts
        ? pts + duration : pts;
    clip.source_video_end = std::max(clip.source_video_end, frame_end);
    if (pts >= clip.stop) return GST_PAD_PROBE_DROP;
    if (!GST_BUFFER_DURATION_IS_VALID(buffer) || GST_BUFFER_DURATION(buffer) == 0) {
        // 표시 길이가 불명확한 frame은 나중에 확인된 공백으로 분류하지 않는다.
        clip.has_uncertain_timing = true;
    }

    GstClockTime clipped_duration = GST_CLOCK_TIME_NONE;
    const auto clipped_start = std::max(pts, clip.start);
    if (GST_CLOCK_TIME_IS_VALID(duration)) {
        const auto end = duration > GST_CLOCK_TIME_NONE - pts
            ? GST_CLOCK_TIME_NONE : pts + duration;
        if (end <= clip.start) return GST_PAD_PROBE_DROP;
        clipped_duration = std::min(end, clip.stop) - clipped_start;
        if (clipped_duration == 0) return GST_PAD_PROBE_DROP;
    } else if (pts < clip.start) {
        // 길이를 모르는 frame은 요청 시작과 겹친다고 추정하지 않는다.
        return GST_PAD_PROBE_DROP;
    }

    buffer = gst_buffer_make_writable(buffer);
    GST_PAD_PROBE_INFO_DATA(info) = buffer;
    GST_BUFFER_PTS(buffer) = clipped_start - clip.start;
    GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DURATION(buffer) = clipped_duration;
    ++clip.kept_frames;
    return GST_PAD_PROBE_OK;
}

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
    if (caps == nullptr || gst_caps_is_empty(caps) || gst_caps_is_any(caps)) {
        return false;
    }

    const guint size = gst_caps_get_size(caps);
    for (guint index = 0; index < size; ++index) {
        const GstStructure* structure = gst_caps_get_structure(caps, index);
        const char* name = structure == nullptr
            ? nullptr
            : gst_structure_get_name(structure);
        if (name != nullptr && std::string(name) == "video/x-h264") {
            return true;
        }
    }
    return false;
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

    // qtdemux의 pad-added 시점에는 current caps가 아직 고정되지 않아
    // query 결과가 ANY일 수 있다. 이 경우 qtdemux의 video_N pad 이름으로
    // 영상 pad를 식별하고, 실제 H264 여부는 뒤의 h264parse가 검증한다.
    const char* pad_name = GST_PAD_NAME(new_pad);
    const bool is_video_pad = pad_name != nullptr &&
        g_str_has_prefix(pad_name, "video_");
    if (!is_h264 && !is_video_pad) {
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

    // 조각 시작점부터 변환하는 remux 경로에서 종료 시각을 제한한다.
    const auto seek_flags = static_cast<GstSeekFlags>(
        GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT
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
    std::int64_t output_offset_ms = 0;
    std::array<int, 8192> counters;
    counters.fill(-1);
    for (const auto& segment : session.segments) {
        auto segment_result = convert_segment_to_ts(segment, output_offset_ms);
        if (segment_result.ok && !continue_ts_counters(segment.output_file_path, counters, segment_result.error_message)) {
            segment_result.ok = false;
        }

        if (segment_result.ok) {
            ++result.success_count;
            output_offset_ms += segment_result.output_duration_ms;
        } else if (segment_result.no_video_in_range) {
            ++result.skipped_count;
        } else {
            ++result.failure_count;
        }

        result.segment_results.push_back(segment_result);
    }

    result.ok = (result.failure_count == 0 && result.success_count > 0);
    if (result.failure_count > 0) {
        std::ostringstream oss;
        oss << "failed to convert "
            << result.failure_count
            << " of "
            << result.requested_count
            << " HLS segments";
        for (const auto& segment_result : result.segment_results) {
            if (!segment_result.ok && !segment_result.no_video_in_range) {
                oss << "; first failed segment " << segment_result.sequence_no
                    << " (offset_ms=" << segment_result.source_offset_ms
                    << ", duration_ms=" << segment_result.requested_duration_ms
                    << "): " << segment_result.error_message;
                break;
            }
        }
        result.error_message = oss.str();
    } else if (result.success_count == 0) {
        result.error_message = "no decoded video frames overlap the requested range";
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
    result.key_unit_seek = (segment.source_offset_ms == 0);

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
    return convert_segment_to_ts(segment, 0);
}

HlsVodConvertResult HlsVodConverter::convert_segment_to_ts(
    const HlsVodSegment& segment, std::int64_t outputOffsetMs
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

    // 녹화 조각 중간에서 시작하는 첫 구간만 정확 절단 경로를 사용한다.
    // 나머지 조각은 기존처럼 decode/re-encode 없는 빠른 remux를 유지한다.
    const bool exact_start_trim = segment.source_offset_ms > 0;

    // 4. GStreamer element 생성
    GstElement* pipeline = gst_pipeline_new("hls-vod-mp4-to-ts-trim-pipeline");
    GstElement* filesrc = gst_element_factory_make("filesrc", "source");
    GstElement* demux = gst_element_factory_make("qtdemux", "demux");
    GstElement* queue = gst_element_factory_make("queue", "video-queue");
    GstElement* input_h264parse = exact_start_trim
        ? gst_element_factory_make("h264parse", "h264-input-parser")
        : nullptr;
    GstElement* decoder = exact_start_trim
        ? gst_element_factory_make("avdec_h264", "h264-decoder")
        : nullptr;
    GstElement* videoconvert = exact_start_trim
        ? gst_element_factory_make("videoconvert", "video-converter")
        : nullptr;
    GstElement* encoder = exact_start_trim
        ? gst_element_factory_make("x264enc", "h264-encoder")
        : nullptr;
    GstElement* h264parse = gst_element_factory_make("h264parse", "h264-output-parser");
    GstElement* capsfilter = gst_element_factory_make("capsfilter", "h264-capsfilter");
    GstElement* mux = gst_element_factory_make("mpegtsmux", "ts-muxer");
    GstElement* filesink = gst_element_factory_make("filesink", "sink");

    if (pipeline == nullptr || filesrc == nullptr || demux == nullptr ||
        queue == nullptr || h264parse == nullptr || capsfilter == nullptr ||
        mux == nullptr || filesink == nullptr ||
        (exact_start_trim && (input_h264parse == nullptr || decoder == nullptr ||
            videoconvert == nullptr || encoder == nullptr))) {
        std::ostringstream missing_elements;
        const auto note_missing = [&missing_elements](const char* name, GstElement* element) {
            if (element == nullptr) {
                if (missing_elements.tellp() > 0) missing_elements << ", ";
                missing_elements << name;
            }
        };
        note_missing("pipeline", pipeline);
        note_missing("filesrc", filesrc);
        note_missing("qtdemux", demux);
        note_missing("queue", queue);
        note_missing("h264parse", h264parse);
        note_missing("capsfilter", capsfilter);
        note_missing("mpegtsmux", mux);
        note_missing("filesink", filesink);
        if (exact_start_trim) {
            note_missing("h264parse (input)", input_h264parse);
            note_missing("avdec_h264", decoder);
            note_missing("videoconvert", videoconvert);
            note_missing("x264enc", encoder);
        }

        unref_object(GST_OBJECT(pipeline));
        unref_object(GST_OBJECT(filesrc));
        unref_object(GST_OBJECT(demux));
        unref_object(GST_OBJECT(queue));
        unref_object(GST_OBJECT(input_h264parse));
        unref_object(GST_OBJECT(decoder));
        unref_object(GST_OBJECT(videoconvert));
        unref_object(GST_OBJECT(encoder));
        unref_object(GST_OBJECT(h264parse));
        unref_object(GST_OBJECT(capsfilter));
        unref_object(GST_OBJECT(mux));
        unref_object(GST_OBJECT(filesink));

        HlsVodConvertResult result = validation;
        result.ok = false;
        result.error_message = "failed to create GStreamer elements: " + missing_elements.str();
        return result;
    }

    // 5. element property 설정
    g_object_set(filesrc, "location", segment.source_file_path.c_str(), nullptr);
    g_object_set(filesink, "location", segment.output_file_path.string().c_str(), nullptr);
    g_object_set(filesink, "sync", FALSE, nullptr);

    // MPEG-TS 안의 H264는 byte-stream/AU 정렬 형태가 다루기 쉽다.
    g_object_set(h264parse, "config-interval", -1, nullptr);
    if (exact_start_trim) {
        // 짧은 첫 구간을 빠르게 다시 인코딩하고, 첫 출력 frame을 IDR로 시작한다.
        gst_util_set_object_arg(G_OBJECT(encoder), "speed-preset", "veryfast");
        gst_util_set_object_arg(G_OBJECT(encoder), "tune", "zerolatency");
        g_object_set(
            encoder,
            "byte-stream", TRUE,
            "key-int-max", 30u,
            nullptr
        );
    }

    GstCaps* h264_caps = gst_caps_from_string(
        "video/x-h264,stream-format=byte-stream,alignment=au"
    );
    g_object_set(capsfilter, "caps", h264_caps, nullptr);
    gst_caps_unref(h264_caps);

    if (exact_start_trim) {
        gst_bin_add_many(
            GST_BIN(pipeline),
            filesrc,
            demux,
            queue,
            input_h264parse,
            decoder,
            videoconvert,
            encoder,
            h264parse,
            capsfilter,
            mux,
            filesink,
            nullptr
        );
    } else {
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
    }

    if (!gst_element_link(filesrc, demux)) {
        gst_object_unref(pipeline);

        HlsVodConvertResult result = validation;
        result.ok = false;
        result.error_message = "failed to link filesrc -> qtdemux";
        return result;
    }

    const bool output_linked = exact_start_trim
        ? gst_element_link_many(
            queue,
            input_h264parse,
            decoder,
            videoconvert,
            encoder,
            h264parse,
            capsfilter,
            mux,
            filesink,
            nullptr)
        : gst_element_link_many(
            queue,
            h264parse,
            capsfilter,
            mux,
            filesink,
            nullptr);
    if (!output_linked) {
        gst_object_unref(pipeline);

        HlsVodConvertResult result = validation;
        result.ok = false;
        result.error_message = exact_start_trim
            ? "failed to link exact trim decode/re-encode pipeline"
            : "failed to link remux pipeline";
        return result;
    }

    // qtdemux의 video pad는 동적으로 생성되므로 pad-added에서 queue와 연결한다.
    QtdemuxLinkContext link_context;
    link_context.queue = queue;
    g_signal_connect(demux, "pad-added", G_CALLBACK(on_qtdemux_pad_added), &link_context);

    RawClipContext raw_clip;
    gst_segment_init(&raw_clip.input_segment, GST_FORMAT_TIME);
    raw_clip.start = milliseconds_to_nanoseconds(segment.source_offset_ms);
    raw_clip.stop = milliseconds_to_nanoseconds(segment.source_offset_ms + segment.duration_ms);
    if (exact_start_trim) {
        GstPad* raw_pad = gst_element_get_static_pad(decoder, "src");
        gst_pad_add_probe(raw_pad,
            static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
            clip_decoded_video, &raw_clip, nullptr);
        gst_object_unref(raw_pad);
    }

    OutputTimeline output_timeline;
    gst_segment_init(&output_timeline.segment, GST_FORMAT_TIME);
    output_timeline.output_base += milliseconds_to_nanoseconds(outputOffsetMs);
    GstPad* output_pad = gst_element_get_static_pad(h264parse, "src");
    gst_pad_add_probe(output_pad,
        static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
        measure_output_timeline, &output_timeline, nullptr);
    gst_object_unref(output_pad);

    GstBus* bus = gst_element_get_bus(pipeline);

    // 6. 정확 절단은 처음부터 디코딩하고, remux만 PAUSED 후 seek한다.
    std::string error_message;
    if (!wait_until_paused(pipeline, bus, link_context, error_message)) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(bus);
        gst_object_unref(pipeline);

        HlsVodConvertResult result = validation;
        result.ok = false;
        result.error_message = "failed to preroll source MP4: " + error_message;
        return result;
    }

    if (!exact_start_trim && !apply_trim_seek(pipeline, segment, error_message)) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(bus);
        gst_object_unref(pipeline);

        HlsVodConvertResult result = validation;
        result.ok = false;
        result.error_message = error_message;
        return result;
    }

    // 7. 요청한 raw frame 또는 seek 구간을 EOS까지 변환한다.
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

    if (exact_start_trim && raw_clip.kept_frames == 0) {
        HlsVodConvertResult result = validation;
        result.ok = false;
        result.no_video_in_range = raw_clip.decoded_frames > 0 && !raw_clip.has_uncertain_timing;
        result.error_message = result.no_video_in_range
            ? "requested time range contains no decoded video frames"
            : "could not establish decoded video coverage for requested time range";
        result.error_message += " (observed_video_end_ms="
            + std::to_string(raw_clip.source_video_end / GST_MSECOND) + ")";
        return result;
    }

    std::error_code output_error;
    const auto output_size = std::filesystem::file_size(segment.output_file_path, output_error);
    if (output_error || output_size == 0) {
        HlsVodConvertResult result = validation;
        result.ok = false;
        result.error_message = "conversion finished but output TS file is missing or empty";
        return result;
    }

    if (output_timeline.invalid_timestamp || !GST_CLOCK_TIME_IS_VALID(output_timeline.first) ||
        output_timeline.end <= output_timeline.first) {
        HlsVodConvertResult result = validation;
        result.ok = false;
        result.error_message = "could not measure output video duration";
        return result;
    }

    HlsVodConvertResult result = validation;
    result.ok = true;
    result.output_duration_ms = static_cast<std::int64_t>(
        (output_timeline.end - output_timeline.first + GST_MSECOND - 1) / GST_MSECOND);
    if (!output_timeline.sps.empty() && !output_timeline.pps.empty()) {
        const auto configuration = std::to_string(output_timeline.sps.size()) + ":"
            + output_timeline.sps + output_timeline.pps;
        gchar* checksum = g_compute_checksum_for_data(G_CHECKSUM_SHA256,
            reinterpret_cast<const guchar*>(configuration.data()), configuration.size());
        result.codec_configuration = checksum;
        g_free(checksum);
    }
    result.trim_applied = true;
    result.key_unit_seek = !exact_start_trim;
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
    std::cout << "output_duration_ms=" << result.output_duration_ms << std::endl;
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
    std::cout << "skipped_count=" << result.skipped_count << std::endl;
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
        std::cout << "    output_duration_ms=" << segment_result.output_duration_ms << std::endl;
        std::cout << "    requested_stop_ms=" << segment_result.requested_stop_ms << std::endl;
        std::cout << "    trim_applied=" << (segment_result.trim_applied ? "true" : "false") << std::endl;
        std::cout << "    key_unit_seek=" << (segment_result.key_unit_seek ? "true" : "false") << std::endl;

        if (!segment_result.ok) {
            std::cout << "    error=" << segment_result.error_message << std::endl;
        }
    }

    std::cout << "==================================" << std::endl;
}
