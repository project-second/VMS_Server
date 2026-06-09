#ifndef MEDIA_LIVE_STREAM_SERVICE_H
#define MEDIA_LIVE_STREAM_SERVICE_H

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// Live stream 수신 transport 정책.
// 초기 구현은 RTSP ingest 검증을 위해 TCP를 기본값으로 사용한다.
enum class MediaRtspTransport {
    Tcp,
    Udp,
};

// Live stream session의 현재 상태.
enum class MediaLiveStreamState {
    Starting,
    Streaming,
    Stopping,
    Stopped,
    Failed,
};

struct MediaLiveStreamOptions {
    std::string streamId;
    std::string cameraId;
    std::string rtspUrl;
    MediaRtspTransport transport = MediaRtspTransport::Tcp;
    std::chrono::milliseconds latency{200};
};

struct MediaLiveStreamSnapshot {
    std::string streamId;
    std::string cameraId;
    std::string rtspUrl;
    MediaLiveStreamState state = MediaLiveStreamState::Stopped;
    std::string lastError;
    std::chrono::system_clock::time_point startedAt{};
};

// RTSP/RTP live stream을 수신하는 가벼운 media ingest service.
//
// 현재 단계에서는 GStreamer pipeline을 이용해 RTSP stream을 받아 fakesink까지
// 흘려보내며, 연결 가능성과 상태 관리만 검증한다. Recorder, relay, playback은
// 이후 이 service 위에 별도 모듈로 확장한다.
class MediaLiveStreamService {
public:
    MediaLiveStreamService();
    ~MediaLiveStreamService();

    MediaLiveStreamService(const MediaLiveStreamService&) = delete;
    MediaLiveStreamService& operator=(const MediaLiveStreamService&) = delete;

    bool startStream(const MediaLiveStreamOptions& options);
    bool stopStream(const std::string& streamId);
    void stopAll();

    std::optional<MediaLiveStreamSnapshot> stream(const std::string& streamId) const;
    std::vector<MediaLiveStreamSnapshot> streams() const;

private:
    struct Session;

    static std::string makePipelineDescription(const MediaLiveStreamOptions& options);
    static MediaLiveStreamSnapshot snapshotFromSession(const Session& session);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<Session>> sessions_;
};

#endif // MEDIA_LIVE_STREAM_SERVICE_H
