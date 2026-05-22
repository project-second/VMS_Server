#include "VmsPlaybackController.h"

VmsPlaybackController::VmsPlaybackController(HttpServer& server) : httpServer(server) {
    handleGetSegmentList();
    handleGetTimelineList();
    handleStreamTimeRange();
    handleDownloadTimeRange();
    handleGetSegment();
    handleSegmentStream();
    handleSegmentDownload();
    handleSegmentThumbnail();
}

VmsPlaybackController::~VmsPlaybackController() {}

void VmsPlaybackController::handleGetSegmentList() {
httpServer.addRoute(HttpMethod::GET, "/devices/{deviceId}/playback/segments", [this](const Http::Request& req) -> Http::Response {
    const std::string* deviceId = req.getPathParam("deviceId");
    int channelId = 2;
    int page = 1;
    int size = 10;

    if (const auto* v = req.getQueryParam("channelId")) { channelId = std::stoi(*v); }

    if (const auto* v = req.getQueryParam("page")) { page = std::stoi(*v); }

    if (const auto* v = req.getQueryParam("size")) { size = std::stoi(*v); }

    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "application/json");
    json response_body = {
        {"ok", true},
        {"data", {
            {"deviceId", deviceId ? std::stoi(*deviceId) : 1},
            {"channelId", channelId},
            {"page", page},
            {"size", size},
            {"totalCount", 124},
            {"totalPages", 13},
            {"items", json::array({
                {
                    {"segmentId", 1001},
                    {"recordStartAt", "2026-05-14T10:00:00+09:00"},
                    {"recordEndAt", "2026-05-14T10:00:10+09:00"},
                    {"durationSeconds", 10},
                    {"eventType", "CONTINUOUS"},
                    {"thumbnailUrl", "/thumbnails/1001.jpg"},
                    {"playbackUrl", "/playback/1001"},
                    {"fileSize", 10485760}
                },
                {
                    {"segmentId", 1002},
                    {"recordStartAt", "2026-05-14T10:00:10+09:00"},
                    {"recordEndAt", "2026-05-14T10:00:20+09:00"},
                    {"durationSeconds", 10},
                    {"eventType", "MOTION"},
                    {"thumbnailUrl", "/thumbnails/1002.jpg"},
                    {"playbackUrl", "/playback/1002"},
                    {"fileSize", 8252411}
                }
            })}
        }},
        {"error", nullptr}
    };
    res.body = response_body.dump();
    return res;
});
}

void VmsPlaybackController::handleGetTimelineList() {
httpServer.addRoute(HttpMethod::GET, "/devices/{deviceId}/playback/timeline", [this](const Http::Request& req) -> Http::Response {
    const std::string* deviceId = req.getPathParam("deviceId");
    int channelId = 1;
    std::string date = "2026-05-14";

    if (const auto* v = req.getQueryParam("channelId")) { channelId = std::stoi(*v); }

    if (const auto* v = req.getQueryParam("date")) { date = *v; }

    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "application/json");
    json response_body = {
        {"ok", true},
        {"data", {
            {"deviceId", deviceId ? std::stoi(*deviceId) : 1},
            {"channelId", channelId},
            {"date", date},
            {"timezone", "Asia/Seoul"},
            {"items", json::array({
                {
                    {"startAt", "2026-05-14T09:00:00+09:00"},
                    {"endAt", "2026-05-14T09:30:00+09:00"},
                    {"recordType", "CONTINUOUS"},
                    {"segmentCount", 180},
                    {"playbackUrl", "/devices/1/playback/stream?channelId=1&from=2026-05-14T09:00:00%2B09:00&to=2026-05-14T09:30:00%2B09:00"}
                },
                {
                    {"startAt", "2026-05-14T10:10:00+09:00"},
                    {"endAt", "2026-05-14T11:00:00+09:00"},
                    {"recordType", "MOTION"},
                    {"segmentCount", 300},
                    {"playbackUrl", "/devices/1/playback/stream?channelId=1&from=2026-05-14T10:10:00%2B09:00&to=2026-05-14T11:00:00%2B09:00"}
                }
            })}
        }},
        {"error", nullptr}
    };
    res.body = response_body.dump();
    return res;
});
}

void VmsPlaybackController::handleStreamTimeRange() {
httpServer.addRoute(HttpMethod::GET, "/devices/{deviceId}/playback/stream", [this](const Http::Request& req) -> Http::Response {
    const std::string* deviceId = req.getPathParam("deviceId");
    int channelId = 1;
    std::string from = "2026-05-14T09:00:00+09:00";
    std::string to = "2026-05-14T09:30:00+09:00";

    if (const auto* v = req.getQueryParam("channelId")) { channelId = std::stoi(*v); }

    if (const auto* v = req.getQueryParam("from")) { from = *v; }

    if (const auto* v = req.getQueryParam("to")) { to = *v; }

    (void)deviceId;
    (void)channelId;
    (void)from;
    (void)to;

    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "application/vnd.apple.mpegurl");
    res.body =
        "#EXTM3U\n"
        "#EXT-X-TARGETDURATION:4\n"
        "\n"
        "#EXT-X-PROGRAM-DATE-TIME:2026-05-14T09:00:00+09:00\n"
        "#EXTINF:4.0,\n"
        "/playbacks/1001/stream\n"
        "\n"
        "#EXT-X-PROGRAM-DATE-TIME:2026-05-14T09:00:04+09:00\n"
        "#EXTINF:4.0,\n"
        "/playbacks/1002/stream\n";
    return res;
});
}

void VmsPlaybackController::handleDownloadTimeRange() {
httpServer.addRoute(HttpMethod::GET, "/devices/{deviceId}/playback/download", [this](const Http::Request& req) -> Http::Response {
    const std::string* deviceId = req.getPathParam("deviceId");
    int channelId = 1;
    std::string from = "2026-05-14T09:00:00+09:00";
    std::string to = "2026-05-14T09:30:00+09:00";

    if (const auto* v = req.getQueryParam("channelId")) { channelId = std::stoi(*v); }

    if (const auto* v = req.getQueryParam("from")) { from = *v; }

    if (const auto* v = req.getQueryParam("to")) { to = *v; }

    (void)deviceId;
    (void)channelId;
    (void)from;
    (void)to;

    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "video/mp4");
    res.headers.set("Content-Disposition", "attachment; filename=\"device_1_ch_1_20260514_090000_093000.mp4\"");
    res.body = "[ merged MP4 binary data ]";
    return res;
});
}

void VmsPlaybackController::handleGetSegment() {
httpServer.addRoute(HttpMethod::GET, "/playbacks/{segmentId}", [this](const Http::Request& req) -> Http::Response {
    const std::string* segmentId = req.getPathParam("segmentId");

    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "application/json");
    json response_body = {
        {"ok", true},
        {"data", {
            {"segmentId", segmentId ? std::stoi(*segmentId) : 1001},
            {"deviceId", 1},
            {"deviceName", "Main Gate CCTV"},
            {"channelId", 2},
            {"channelName", "CH-02"},
            {"recordStartAt", "2026-05-14T09:00:00+09:00"},
            {"recordEndAt", "2026-05-14T09:00:10+09:00"},
            {"durationSeconds", 10},
            {"recordType", "CONTINUOUS"},
            {"eventType", nullptr},
            {"codec", "H264"},
            {"container", "MP4"},
            {"resolution", "1920x1080"},
            {"fps", 30},
            {"fileSize", 10485760},
            {"thumbnailUrl", "/playbacks/1001/thumbnail"},
            {"streamUrl", "/playbacks/1001/stream"},
            {"downloadUrl", "/playbacks/1001/download"},
            {"createdAt", "2026-05-14T09:00:11+09:00"}
        }},
        {"error", nullptr}
    };
    res.body = response_body.dump();
    return res;
});
}

void VmsPlaybackController::handleSegmentStream() {
httpServer.addRoute(HttpMethod::GET, "/playbacks/{segmentId}/stream", [this](const Http::Request& req) -> Http::Response {
    const std::string* segmentId = req.getPathParam("segmentId");
    (void)segmentId;

    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "video/mp4");
    res.headers.set("Cache-Control", "private, max-age=3600");
    res.headers.set("Accept-Ranges", "bytes");
    res.body = "[ MP4 stream binary data ]";
    return res;
});
}

void VmsPlaybackController::handleSegmentDownload() {
httpServer.addRoute(HttpMethod::GET, "/playbacks/{segmentId}/download", [this](const Http::Request& req) -> Http::Response {
    const std::string* segmentId = req.getPathParam("segmentId");
    (void)segmentId;

    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "video/mp4");
    res.headers.set("Content-Disposition", "attachment; filename=\"cam01_20260514_090000.mp4\"");
    res.body = "[ MP4 binary data ]";
    return res;
});
}

void VmsPlaybackController::handleSegmentThumbnail() {
httpServer.addRoute(HttpMethod::GET, "/playbacks/{segmentId}/thumbnail", [this](const Http::Request& req) -> Http::Response {
    const std::string* segmentId = req.getPathParam("segmentId");
    int width = 480;
    int height = 360;

    if (const auto* v = req.getQueryParam("width")) { width = std::stoi(*v); }

    if (const auto* v = req.getQueryParam("height")) { height = std::stoi(*v); }

    if (const auto* v = req.getQueryParam("hight")) { height = std::stoi(*v); }

    (void)segmentId;
    (void)width;
    (void)height;

    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "image/jpeg");
    res.headers.set("Cache-Control", "public, max-age=86400");
    res.body = "[ JPEG binary data ]";
    return res;
});
}
