#include "VmsPlaybackController.h"

#include "StringHelpers.h"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>

using json = nlohmann::json;

namespace {

Http::Response jsonResponse(HttpCode code, const json& data) {
    Http::Response response;
    response.setStatus(code);
    response.headers.set("Content-Type", "application/json");
    response.body = json{{"ok", true}, {"data", data}, {"error", nullptr}}.dump();
    return response;
}

Http::Response errorResponse(HttpCode code, const std::string& errorCode,
                             const std::string& message) {
    Http::Response response;
    response.setStatus(code);
    response.headers.set("Content-Type", "application/json");
    response.body = json{{"ok", false}, {"data", nullptr},
        {"error", {{"code", errorCode}, {"message", message}}}}.dump();
    return response;
}

std::optional<std::int64_t> integer(const std::string* value) {
    if (!value) return std::nullopt;
    std::int64_t parsed = 0;
    if (!parse_int64_strict(*value, parsed)) return std::nullopt;
    return parsed;
}

std::optional<std::int64_t> parseIso8601(const std::string& value) {
    static const std::regex pattern(
        R"(^([0-9]{4})-([0-9]{2})-([0-9]{2})T([0-9]{2}):([0-9]{2}):([0-9]{2})(?:\.([0-9]{1,3}))?(Z|z|([+-])([0-9]{2}):([0-9]{2}))$)");
    std::smatch match;
    if (!std::regex_match(value, match, pattern)) return std::nullopt;
    std::tm tm{};
    tm.tm_year = std::stoi(match[1]) - 1900;
    tm.tm_mon = std::stoi(match[2]) - 1;
    tm.tm_mday = std::stoi(match[3]);
    tm.tm_hour = std::stoi(match[4]);
    tm.tm_min = std::stoi(match[5]);
    tm.tm_sec = std::stoi(match[6]);
    tm.tm_isdst = 0;
    const std::tm requested = tm;
    const auto seconds = timegm(&tm);
    if (seconds == static_cast<std::time_t>(-1)) return std::nullopt;
    std::tm normalized{};
    gmtime_r(&seconds, &normalized);
    if (normalized.tm_year != requested.tm_year || normalized.tm_mon != requested.tm_mon ||
        normalized.tm_mday != requested.tm_mday || normalized.tm_hour != requested.tm_hour ||
        normalized.tm_min != requested.tm_min || normalized.tm_sec != requested.tm_sec) {
        return std::nullopt;
    }
    std::int64_t milliseconds = static_cast<std::int64_t>(seconds) * 1000;
    if (match[7].matched) {
        std::string fraction = match[7];
        while (fraction.size() < 3) fraction.push_back('0');
        milliseconds += std::stoi(fraction);
    }
    if (match[9].matched) {
        const auto offset = (std::stoi(match[10]) * 60 + std::stoi(match[11])) * 60LL * 1000LL;
        milliseconds += match[9].str() == "+" ? -offset : offset;
    }
    return milliseconds;
}

std::string formatSeoul(std::int64_t epochMs) {
    const auto localMs = epochMs + 9LL * 60LL * 60LL * 1000LL;
    const std::time_t seconds = static_cast<std::time_t>(localMs / 1000);
    std::tm tm{};
    gmtime_r(&seconds, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << "+09:00";
    return out.str();
}

std::string encodeQuery(const std::string& value) {
    std::ostringstream out;
    out << std::uppercase << std::hex;
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out << static_cast<char>(ch);
        } else {
            out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
    }
    return out.str();
}

json segmentJson(const PlaybackSegmentRecord& segment) {
    const auto endMs = segment.endMs.value_or(segment.startMs);
    return {
        {"segmentId", segment.segmentId},
        {"recordStartAt", formatSeoul(segment.startMs)},
        {"recordEndAt", segment.endMs ? json(formatSeoul(*segment.endMs)) : json(nullptr)},
        {"durationSeconds", std::max<std::int64_t>(0, endMs - segment.startMs) / 1000.0},
        {"eventType", "CONTINUOUS"},
        {"thumbnailUrl", nullptr},
        {"playbackUrl", nullptr},
        {"fileSize", segment.fileSize},
        {"status", segment.status}
    };
}

} // namespace

VmsPlaybackController::VmsPlaybackController(
    HttpServer& server, VmsPlaybackService& service)
    : httpServer_(server), service_(service) {
    handleGetSegmentList();
    handleGetTimelineList();
    handleStreamTimeRange();
    handleGetSegment();
    handleHlsSegment();
}

void VmsPlaybackController::handleGetSegmentList() {
    httpServer_.addRoute(HttpMethod::GET, "/devices/{deviceId}/playback/segments",
        [this](const Http::Request& req) {
        const auto deviceId = integer(req.getPathParam("deviceId"));
        const auto channelId = integer(req.getQueryParam("channelId"));
        auto page = integer(req.getQueryParam("page")).value_or(1);
        auto size = integer(req.getQueryParam("size")).value_or(10);
        if (!deviceId || !channelId || *deviceId <= 0 || *channelId <= 0 ||
            page <= 0 || size <= 0 || size > 100) {
            return errorResponse(HttpCode::Bad_Request, "INVALID_REQUEST",
                "deviceId/channelId must be positive; page >= 1 and size must be 1..100");
        }
        const auto target = service_.findTarget(*deviceId, *channelId);
        if (!target) return errorResponse(HttpCode::Not_Found, "PLAYBACK_TARGET_NOT_FOUND",
            "enabled device/channel/profile was not found");
        const auto result = service_.findPage(target->profileId, page, size);
        json items = json::array();
        for (const auto& item : result.items) items.push_back(segmentJson(item));
        return jsonResponse(HttpCode::OK, {
            {"deviceId", *deviceId}, {"channelId", *channelId},
            {"page", page}, {"size", size}, {"totalCount", result.totalCount},
            {"totalPages", size ? (result.totalCount + size - 1) / size : 0},
            {"items", items}});
    });
}

void VmsPlaybackController::handleGetTimelineList() {
    httpServer_.addRoute(HttpMethod::GET, "/devices/{deviceId}/playback/timeline",
        [this](const Http::Request& req) {
        const auto deviceId = integer(req.getPathParam("deviceId"));
        const auto channelId = integer(req.getQueryParam("channelId"));
        const auto* date = req.getQueryParam("date");
        if (!deviceId || !channelId || !date) return errorResponse(
            HttpCode::Bad_Request, "INVALID_REQUEST", "deviceId, channelId and date are required");
        const auto from = parseIso8601(*date + "T00:00:00+09:00");
        if (!from || date->size() != 10) return errorResponse(
            HttpCode::Bad_Request, "INVALID_DATE", "date must be YYYY-MM-DD");
        const auto target = service_.findTarget(*deviceId, *channelId);
        if (!target) return errorResponse(HttpCode::Not_Found, "PLAYBACK_TARGET_NOT_FOUND",
            "enabled device/channel/profile was not found");
        const auto to = *from + 24LL * 60LL * 60LL * 1000LL;
        const auto segments = service_.findRange(target->profileId, *from, to);
        json items = json::array();
        std::size_t index = 0;
        while (index < segments.size()) {
            auto rangeStart = segments[index].startMs;
            auto rangeEnd = segments[index].endMs.value_or(rangeStart);
            std::size_t count = 1;
            ++index;
            while (index < segments.size() && segments[index].startMs <= rangeEnd + 1500) {
                rangeEnd = std::max(rangeEnd, segments[index].endMs.value_or(segments[index].startMs));
                ++count;
                ++index;
            }
            const auto fromText = formatSeoul(rangeStart);
            const auto toText = formatSeoul(rangeEnd);
            std::ostringstream url;
            url << "/devices/" << *deviceId << "/playback/stream?channelId=" << *channelId
                << "&from=" << encodeQuery(fromText) << "&to=" << encodeQuery(toText);
            items.push_back({{"startAt", fromText}, {"endAt", toText},
                {"recordType", "CONTINUOUS"}, {"segmentCount", count},
                {"playbackUrl", url.str()}});
        }
        return jsonResponse(HttpCode::OK, {{"deviceId", *deviceId}, {"channelId", *channelId},
            {"date", *date}, {"timezone", "Asia/Seoul"}, {"items", items}});
    });
}

void VmsPlaybackController::handleStreamTimeRange() {
    httpServer_.addRoute(HttpMethod::GET, "/devices/{deviceId}/playback/stream",
        [this](const Http::Request& req) {
        const auto deviceId = integer(req.getPathParam("deviceId"));
        const auto channelId = integer(req.getQueryParam("channelId"));
        const auto* fromText = req.getQueryParam("from");
        const auto* toText = req.getQueryParam("to");
        if (!deviceId || !channelId || !fromText || !toText) return errorResponse(
            HttpCode::Bad_Request, "INVALID_REQUEST", "deviceId, channelId, from and to are required");
        const auto from = parseIso8601(*fromText);
        const auto to = parseIso8601(*toText);
        if (!from || !to || *from >= *to || *to - *from > 60LL * 60LL * 1000LL) return errorResponse(
            HttpCode::Bad_Request, "INVALID_TIME_RANGE",
            "from/to must be valid ISO-8601; the range must be greater than 0 and at most 1 hour");
        const auto target = service_.findTarget(*deviceId, *channelId);
        if (!target) return errorResponse(HttpCode::Not_Found, "PLAYBACK_TARGET_NOT_FOUND",
            "enabled device/channel/profile was not found");
        auto hls = service_.createHls(*target, *from, *to);
        if (!hls.ok) return errorResponse(
            hls.notFound ? HttpCode::Not_Found : HttpCode::Internal_Server_Error,
            hls.notFound ? "RECORDING_NOT_FOUND" : "HLS_GENERATION_FAILED", hls.errorMessage);
        Http::Response response;
        response.setStatus(HttpCode::OK);
        response.headers.set("Content-Type", "application/vnd.apple.mpegurl");
        response.headers.set("Cache-Control", "no-store");
        response.headers.set("X-Playback-Session-Id", hls.sessionId);
        response.body = std::move(hls.playlist);
        return response;
    });
}

void VmsPlaybackController::handleGetSegment() {
    httpServer_.addRoute(HttpMethod::GET, "/playbacks/{segmentId}",
        [this](const Http::Request& req) {
        const auto segmentId = integer(req.getPathParam("segmentId"));
        if (!segmentId || *segmentId <= 0) return errorResponse(
            HttpCode::Bad_Request, "INVALID_SEGMENT_ID", "segmentId must be positive");
        const auto segment = service_.findSegment(*segmentId);
        if (!segment) return errorResponse(HttpCode::Not_Found, "SEGMENT_NOT_FOUND",
            "recording segment was not found");
        auto data = segmentJson(*segment);
        data["deviceId"] = segment->deviceId;
        data["deviceName"] = segment->deviceName;
        data["channelId"] = segment->channelId;
        data["channelName"] = segment->channelName;
        data["recordType"] = "CONTINUOUS";
        data["codec"] = segment->codec;
        data["container"] = "MP4";
        return jsonResponse(HttpCode::OK, data);
    });
}

void VmsPlaybackController::handleHlsSegment() {
    httpServer_.addRoute(HttpMethod::GET,
        "/playback/sessions/{sessionId}/segments/{fileName}",
        [this](const Http::Request& req) {
        const auto* sessionId = req.getPathParam("sessionId");
        const auto* fileName = req.getPathParam("fileName");
        const auto* token = req.getQueryParam("token");
        if (!sessionId || !fileName) return errorResponse(
            HttpCode::Bad_Request, "INVALID_HLS_SEGMENT", "sessionId and fileName are required");
        if (!token || token->empty()) return errorResponse(
            HttpCode::Unauthorized, "INVALID_HLS_TOKEN", "HLS session token is required");
        auto result = service_.readHlsSegment(*sessionId, *fileName, *token);
        if (!result.ok) return errorResponse(
            result.unauthorized ? HttpCode::Unauthorized : HttpCode::Not_Found,
            result.unauthorized ? "INVALID_HLS_TOKEN" : "HLS_SEGMENT_NOT_FOUND",
            result.errorMessage);
        Http::Response response;
        response.setStatus(HttpCode::OK);
        response.headers.set("Content-Type", "video/mp2t");
        response.headers.set("Cache-Control", "private, max-age=900");
        response.body = std::move(result.body);
        return response;
    });
}
