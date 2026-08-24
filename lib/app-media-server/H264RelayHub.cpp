#include "H264RelayHub.h"

#include <iostream>

H264RelayHub::~H264RelayHub() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [camera_id, state] : streams_) {
        (void)camera_id;
        clear_state(state);
    }
}

void H264RelayHub::attach_appsrc(
    const std::string& camera_id,
    GstAppSrc* appsrc
) {
    if (camera_id.empty() || appsrc == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    StreamState& state = streams_[camera_id];

    if (state.appsrc != appsrc) {
        if (state.appsrc != nullptr) {
            gst_object_unref(state.appsrc);
        }

        state.appsrc = GST_APP_SRC(gst_object_ref(appsrc));
    }

    gst_app_src_set_stream_type(appsrc, GST_APP_STREAM_TYPE_STREAM);
    gst_app_src_set_emit_signals(appsrc, FALSE);
    gst_app_src_set_max_buffers(appsrc, 60);
    gst_app_src_set_leaky_type(appsrc, GST_APP_LEAKY_TYPE_DOWNSTREAM);

    g_object_set(
        appsrc,
        "is-live", TRUE,
        "format", GST_FORMAT_TIME,
        "do-timestamp", TRUE,
        "block", FALSE,
        nullptr
    );

    if (state.caps != nullptr) {
        gst_app_src_set_caps(appsrc, state.caps);
    }

    state.waiting_for_keyframe = true;
}

void H264RelayHub::detach_appsrc(
    const std::string& camera_id,
    GstAppSrc* appsrc
) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto found = streams_.find(camera_id);
    if (found == streams_.end() || found->second.appsrc != appsrc) {
        return;
    }

    gst_object_unref(found->second.appsrc);
    found->second.appsrc = nullptr;
    found->second.waiting_for_keyframe = true;
}

void H264RelayHub::publish_sample(
    const std::string& camera_id,
    GstSample* sample
) {
    if (camera_id.empty() || sample == nullptr) {
        return;
    }

    GstBuffer* source_buffer = gst_sample_get_buffer(sample);
    GstCaps* source_caps = gst_sample_get_caps(sample);

    if (source_buffer == nullptr || source_caps == nullptr) {
        return;
    }

    const bool is_keyframe = !GST_BUFFER_FLAG_IS_SET(
        source_buffer,
        GST_BUFFER_FLAG_DELTA_UNIT
    );

    GstAppSrc* appsrc = nullptr;
    bool first_forwarded_buffer = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        StreamState& state = streams_[camera_id];

        if (state.caps == nullptr || !gst_caps_is_equal(state.caps, source_caps)) {
            if (state.caps != nullptr) {
                gst_caps_unref(state.caps);
            }

            state.caps = gst_caps_ref(source_caps);
            state.waiting_for_keyframe = true;

            if (state.appsrc != nullptr) {
                gst_app_src_set_caps(state.appsrc, state.caps);
            }
        }

        if (state.appsrc == nullptr) {
            return;
        }

        if (state.waiting_for_keyframe && !is_keyframe) {
            return;
        }

        first_forwarded_buffer = state.waiting_for_keyframe;
        state.waiting_for_keyframe = false;
        appsrc = GST_APP_SRC(gst_object_ref(state.appsrc));
    }

    // GstPipeline 경계를 넘기므로 source pipeline의 running-time 대신
    // appsrc pipeline clock 기준으로 timestamp를 다시 부여한다.
    GstBuffer* relay_buffer = gst_buffer_copy(source_buffer);
    if (relay_buffer == nullptr) {
        gst_object_unref(appsrc);
        return;
    }

    GST_BUFFER_PTS(relay_buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DTS(relay_buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_OFFSET(relay_buffer) = GST_BUFFER_OFFSET_NONE;
    GST_BUFFER_OFFSET_END(relay_buffer) = GST_BUFFER_OFFSET_NONE;

    if (first_forwarded_buffer) {
        GST_BUFFER_FLAG_SET(relay_buffer, GST_BUFFER_FLAG_DISCONT);
    }

    const GstFlowReturn flow = gst_app_src_push_buffer(appsrc, relay_buffer);

    if (flow != GST_FLOW_OK && flow != GST_FLOW_FLUSHING && flow != GST_FLOW_EOS) {
        std::cerr << "H264RelayHub: failed to push H264 buffer. camera_id="
                  << camera_id
                  << ", flow="
                  << gst_flow_get_name(flow)
                  << std::endl;
    }

    gst_object_unref(appsrc);
}

void H264RelayHub::reset_source(const std::string& camera_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto found = streams_.find(camera_id);
    if (found == streams_.end()) {
        return;
    }

    if (found->second.caps != nullptr) {
        gst_caps_unref(found->second.caps);
        found->second.caps = nullptr;
    }

    found->second.waiting_for_keyframe = true;
}

void H264RelayHub::clear_state(StreamState& state) {
    if (state.appsrc != nullptr) {
        gst_object_unref(state.appsrc);
        state.appsrc = nullptr;
    }

    if (state.caps != nullptr) {
        gst_caps_unref(state.caps);
        state.caps = nullptr;
    }

    state.waiting_for_keyframe = true;
}
