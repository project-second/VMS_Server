#include "VmsEventController.h"

VmsEventController::VmsEventController(HttpServer& server) : httpServer(server) {
    handleGetEventList();
    handleGetEventDetails();
    handleEventSubscription();
}

VmsEventController::~VmsEventController() {}

void VmsEventController::handleGetEventList() {
httpServer.addRoute(HttpMethod::GET, "/events", [this](const Http::Request& req) -> Http::Response {
    int page = 1;
    int size = 10;

    if (const auto* v = req.getQueryParam("page")) { page = std::stoi(*v); }

    if (const auto* v = req.getQueryParam("size")) { size = std::stoi(*v); }

    (void)page;
    (void)size;

    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "application/json");
    json response_body = {
        {"ok", true},
        {"data", json::array({
            {
                {"channelId", 34},
                {"eventId", 101},
                {"eventType", "산불"},
                {"eventTime", "이벤트 발생 기준 UTC 시간"}
            },
            {
                {"channelId", 1},
                {"eventId", 102},
                {"eventType", "모션감지"},
                {"eventTime", "이벤트 발생 기준 UTC 시간"}
            },
            {
                {"channelId", 1},
                {"eventId", 103},
                {"eventType", "산사태"},
                {"eventTime", "이벤트 발생 기준 UTC 시간"}
            }
        })},
        {"error", nullptr}
    };
    res.body = response_body.dump();
    return res;
});
}

void VmsEventController::handleGetEventDetails() {
httpServer.addRoute(HttpMethod::GET, "/events/{eventId}", [this](const Http::Request& req) -> Http::Response {
    const std::string* eventId = req.getPathParam("eventId");

    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "application/json");
    json response_body = {
        {"ok", true},
        {"data", {
            {"channelId", 34},
            {"eventId", eventId ? std::stoi(*eventId) : 101},
            {"eventType", "산불"},
            {"eventTime", "이벤트 발생 기준 UTC 시간"},
            {"summary", "요약 정보"},
            {"bestshotId", 342324}
        }},
        {"error", nullptr}
    };
    res.body = response_body.dump();
    return res;
});
}

void VmsEventController::handleEventSubscription() {
httpServer.addWebSocketRoute(
    "/events/ws",
    [this](WebSocketConnection& ws, const Http::Request& req) {
        (void)req;

        // TODO 아래는 예시
        json event_body = {
            {"ok", true},
            {"data", {
                {"channelId", 34},
                {"eventId", 101},
                {"eventType", "산불"},
                {"eventTime", "이벤트 발생 기준 UTC 시간"},
                {"summary", "요약 정보"},
                {"bestshotId", 342324}
            }},
            {"error", nullptr}
        };

        ws.sendText(event_body.dump());
        ws.close(1000, "normal closure");
    },
    [this](const Http::Request& req) -> std::optional<Http::Response> {
        (void)req;

        // TODO 인증 실패 시 401/403 응답 처리
        return std::nullopt;
    });
}
