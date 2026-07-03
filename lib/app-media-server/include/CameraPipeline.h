#ifndef __CAMERAPIPELINE_H__
#define __CAMERAPIPELINE_H__

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include <string>

#include "RecordingSegmentStore.h"

enum class CameraPipelineEvent {
    None,
    Error,
    Eos
};

class CameraPipeline {
private:
    std::string camera_id_;
    std::string rtsp_uri_;
    std::string record_pattern_;
    int split_seconds_ = 5;

    RecordingSegmentStore* segment_store_ = nullptr;

    int frame_count_ = 0;

    GstElement* pipeline_ = nullptr;

    GstElement* source_ = nullptr;
    GstElement* depay_ = nullptr;
    GstElement* parser_ = nullptr;
    GstElement* tee_ = nullptr;

    GstElement* record_queue_ = nullptr;
    GstElement* split_sink_ = nullptr;

    GstElement* app_queue_ = nullptr;
    GstElement* decoder_ = nullptr;
    GstElement* converter_ = nullptr;
    GstElement* videoscale_ = nullptr;
    GstElement* capsfilter_ = nullptr;
    GstElement* app_sink_ = nullptr;

    GstPad* tee_record_pad_ = nullptr;
    GstPad* tee_app_pad_ = nullptr;

    GstPad* record_queue_sink_pad_ = nullptr;
    GstPad* app_queue_sink_pad_ = nullptr;

public:
    CameraPipeline(
        std::string camera_id,
        std::string rtsp_uri,
        std::string record_pattern,
        int split_seconds,
        RecordingSegmentStore* segment_store
    );

    ~CameraPipeline();

    CameraPipeline(const CameraPipeline&) = delete;
    CameraPipeline& operator=(const CameraPipeline&) = delete;

    bool create();
    bool start();

    CameraPipelineEvent poll_bus(int timeout_ms);

    void stop();

private:
    static void on_pad_added(
        GstElement* src,
        GstPad* new_pad,
        gpointer user_data
    );

    static GstFlowReturn on_new_sample(
        GstAppSink* appsink,
        gpointer user_data
    );

    static gchar* on_format_location(
        GstElement* splitmux,
        guint fragment_id,
        gpointer user_data
    );

    void handle_pad_added(GstPad* new_pad);
    GstFlowReturn handle_new_sample(GstAppSink* appsink);
    gchar* handle_format_location(guint fragment_id);

    bool finalize_recording(int timeout_ms);
    void handle_bus_message(GstMessage* msg);
    void handle_element_message(GstMessage* msg);

    bool create_elements();
    bool configure_elements();
    bool add_elements_to_pipeline();
    bool link_static_elements();
    bool link_tee_branches();
};

#endif // __CAMERAPIPELINE_H__