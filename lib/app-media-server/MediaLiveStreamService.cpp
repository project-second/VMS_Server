#include "MediaLiveStreamService.h"

#include <atomic>
#include <sstream>
#include <thread>
#include <utility>

#include <gst/gst.h>

#include "LogSync.h"

namespace {

std::once_flag gstreamerInitOnce;

void ensureGStreamerInitialized() {
    std::call_once(gstreamerInitOnce, []() {
        gst_init(nullptr, nullptr);
    });
}

std::string shellQuoteForGst(const std::string& value) {
    std::string quoted = "\"";
    for (const char ch : value) {
        if (ch == '\\' || ch == '"') {
            quoted.push_back('\\');
        }
        quoted.push_back(ch);
    }
    quoted.push_back('"');
    return quoted;
}

std::string gstErrorMessage(GError* error, const gchar* debug) {
    std::ostringstream oss;
    if (error != nullptr && error->message != nullptr) {
        oss << error->message;
    } else {
        oss << "unknown GStreamer error";
    }

    if (debug != nullptr) {
        oss << " (" << debug << ")";
    }

    return oss.str();
}

} // namespace

struct MediaLiveStreamService::Session {
    mutable std::mutex mutex;
    MediaLiveStreamOptions options;
    MediaLiveStreamState state = MediaLiveStreamState::Starting;
    std::string lastError;
    std::chrono::system_clock::time_point startedAt{};
    GstElement* pipeline = nullptr;
    GstBus* bus = nullptr;
    std::atomic<bool> running{false};
    std::thread busThread;
};

MediaLiveStreamService::MediaLiveStreamService() {
    ensureGStreamerInitialized();
}

MediaLiveStreamService::~MediaLiveStreamService() {
    stopAll();
}

bool MediaLiveStreamService::startStream(const MediaLiveStreamOptions& options) {
    if (options.streamId.empty() || options.rtspUrl.empty()) {
        LogSync::error("[MEDIA] startStream failed: streamId and rtspUrl are required");
        return false;
    }

    ensureGStreamerInitialized();

    std::lock_guard<std::mutex> lock(mutex_);
    if (sessions_.find(options.streamId) != sessions_.end()) {
        LogSync::error("[MEDIA] startStream failed: duplicate streamId=", options.streamId);
        return false;
    }

    GError* error = nullptr;
    const std::string pipelineDescription = makePipelineDescription(options);
    GstElement* pipeline = gst_parse_launch(pipelineDescription.c_str(), &error);
    if (pipeline == nullptr) {
        const std::string message = gstErrorMessage(error, nullptr);
        if (error != nullptr) {
            g_error_free(error);
        }
        LogSync::error("[MEDIA] startStream pipeline create failed: ", message);
        return false;
    }

    if (error != nullptr) {
        LogSync::error("[MEDIA] startStream pipeline warning: ", error->message);
        g_error_free(error);
    }

    GstBus* bus = gst_element_get_bus(pipeline);
    auto session = std::make_unique<Session>();
    session->options = options;
    session->pipeline = pipeline;
    session->bus = bus;
    session->running = true;
    session->startedAt = std::chrono::system_clock::now();

    Session* sessionPtr = session.get();
    sessionPtr->busThread = std::thread([sessionPtr]() {
        while (sessionPtr->running.load()) {
            GstMessage* message = gst_bus_timed_pop(sessionPtr->bus, 200 * GST_MSECOND);
            if (message == nullptr) {
                continue;
            }

            switch (GST_MESSAGE_TYPE(message)) {
            case GST_MESSAGE_ERROR: {
                GError* error = nullptr;
                gchar* debug = nullptr;
                gst_message_parse_error(message, &error, &debug);
                {
                    std::lock_guard<std::mutex> lock(sessionPtr->mutex);
                    sessionPtr->lastError = gstErrorMessage(error, debug);
                    sessionPtr->state = MediaLiveStreamState::Failed;
                }
                sessionPtr->running = false;
                LogSync::error("[MEDIA] stream failed streamId=", sessionPtr->options.streamId,
                    " error=", sessionPtr->lastError);
                if (error != nullptr) {
                    g_error_free(error);
                }
                if (debug != nullptr) {
                    g_free(debug);
                }
                break;
            }
            case GST_MESSAGE_EOS:
                {
                    std::lock_guard<std::mutex> lock(sessionPtr->mutex);
                    sessionPtr->state = MediaLiveStreamState::Stopped;
                }
                sessionPtr->running = false;
                LogSync::error("[MEDIA] stream EOS streamId=", sessionPtr->options.streamId);
                break;
            case GST_MESSAGE_STATE_CHANGED:
                if (GST_MESSAGE_SRC(message) == GST_OBJECT(sessionPtr->pipeline)) {
                    GstState oldState;
                    GstState newState;
                    GstState pending;
                    gst_message_parse_state_changed(message, &oldState, &newState, &pending);
                    if (newState == GST_STATE_PLAYING) {
                        std::lock_guard<std::mutex> lock(sessionPtr->mutex);
                        sessionPtr->state = MediaLiveStreamState::Streaming;
                    }
                }
                break;
            default:
                break;
            }

            gst_message_unref(message);
        }
    });

    const GstStateChangeReturn stateResult = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (stateResult == GST_STATE_CHANGE_FAILURE) {
        session->running = false;
        if (session->busThread.joinable()) {
            session->busThread.join();
        }
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(bus);
        gst_object_unref(pipeline);
        LogSync::error("[MEDIA] startStream failed: cannot set pipeline PLAYING streamId=", options.streamId);
        return false;
    }

    LogSync::info("[MEDIA] stream started streamId=", options.streamId,
        " transport=", options.transport == MediaRtspTransport::Tcp ? "tcp" : "udp");
    sessions_.emplace(options.streamId, std::move(session));
    return true;
}

bool MediaLiveStreamService::stopStream(const std::string& streamId) {
    std::unique_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(streamId);
        if (it == sessions_.end()) {
            return false;
        }
        session = std::move(it->second);
        sessions_.erase(it);
    }

    {
        std::lock_guard<std::mutex> lock(session->mutex);
        session->state = MediaLiveStreamState::Stopping;
    }
    session->running = false;
    gst_element_set_state(session->pipeline, GST_STATE_NULL);

    if (session->busThread.joinable()) {
        session->busThread.join();
    }

    if (session->bus != nullptr) {
        gst_object_unref(session->bus);
    }
    if (session->pipeline != nullptr) {
        gst_object_unref(session->pipeline);
    }

    LogSync::info("[MEDIA] stream stopped streamId=", streamId);
    return true;
}

void MediaLiveStreamService::stopAll() {
    std::vector<std::string> ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ids.reserve(sessions_.size());
        for (const auto& item : sessions_) {
            ids.push_back(item.first);
        }
    }

    for (const std::string& id : ids) {
        stopStream(id);
    }
}

std::optional<MediaLiveStreamSnapshot> MediaLiveStreamService::stream(const std::string& streamId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = sessions_.find(streamId);
    if (it == sessions_.end()) {
        return std::nullopt;
    }
    return snapshotFromSession(*it->second);
}

std::vector<MediaLiveStreamSnapshot> MediaLiveStreamService::streams() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<MediaLiveStreamSnapshot> result;
    result.reserve(sessions_.size());
    for (const auto& item : sessions_) {
        result.push_back(snapshotFromSession(*item.second));
    }
    return result;
}

std::string MediaLiveStreamService::makePipelineDescription(const MediaLiveStreamOptions& options) {
    const char* protocols = options.transport == MediaRtspTransport::Tcp ? "tcp" : "udp";
    const auto latencyMs = static_cast<long long>(options.latency.count());

    std::ostringstream oss;
    oss << "rtspsrc location=" << shellQuoteForGst(options.rtspUrl)
        << " protocols=" << protocols
        << " latency=" << latencyMs
        << " name=src "
        << "src. ! application/x-rtp,media=video,encoding-name=H264 ! "
        << "queue ! rtph264depay ! h264parse ! fakesink sync=false async=false "
        << "src. ! application/x-rtp,media=video,encoding-name=H265 ! "
        << "queue ! rtph265depay ! h265parse ! fakesink sync=false async=false";
    return oss.str();
}

MediaLiveStreamSnapshot MediaLiveStreamService::snapshotFromSession(const Session& session) {
    std::lock_guard<std::mutex> lock(session.mutex);
    MediaLiveStreamSnapshot snapshot;
    snapshot.streamId = session.options.streamId;
    snapshot.cameraId = session.options.cameraId;
    snapshot.rtspUrl = session.options.rtspUrl;
    snapshot.state = session.state;
    snapshot.lastError = session.lastError;
    snapshot.startedAt = session.startedAt;
    return snapshot;
}
