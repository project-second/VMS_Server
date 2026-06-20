#include "VmsDeviceController.h"

#include "VmsApiResponses.h"
#include "VmsRequestHelpers.h"
#include "StringHelpers.h"

#include <optional>

namespace {

std::optional<float> jsonFloatField(const json& body, const char* key) {
    if (!body.contains(key) || !body[key].is_number()) {
        return std::nullopt;
    }
    return body[key].get<float>();
}

std::optional<float> jsonPtzVelocityValue(const json& body) {
    const auto value = jsonFloatField(body, "value");
    if (!value || *value < -100.0f || *value > 100.0f) {
        return std::nullopt;
    }
    return *value / 100.0f;
}

std::optional<bool> jsonBoolField(const json& body, const char* key) {
    if (!body.contains(key)) {
        return std::nullopt;
    }
    if (!body[key].is_boolean()) {
        return std::nullopt;
    }
    return body[key].get<bool>();
}

Http::Response ptzControlResponse(VmsDeviceService::PtzControlStatus status) {
    if (status == VmsDeviceService::PtzControlStatus::Success) {
        return VmsApiResponse::successResponse({{"result", "OK"}});
    }
    if (status == VmsDeviceService::PtzControlStatus::NotFound) {
        return VmsApiResponse::notFoundResponse();
    }
    if (status == VmsDeviceService::PtzControlStatus::InvalidArgument) {
        return VmsApiResponse::invalidArgumentResponse();
    }
    if (status == VmsDeviceService::PtzControlStatus::Unsupported) {
        return VmsApiResponse::conflictResponse("PTZ_UNSUPPORTED");
    }
    if (status == VmsDeviceService::PtzControlStatus::Busy) {
        return VmsApiResponse::conflictResponse("PTZ_BUSY");
    }
    if (status == VmsDeviceService::PtzControlStatus::UpstreamError) {
        return VmsApiResponse::errorResponse(HttpCode::Bad_Gateway, "PTZ_UPSTREAM_ERROR");
    }
    return VmsApiResponse::internalServerErrorResponse();
}

} // namespace

VmsDeviceController::VmsDeviceController(HttpServer& server, VmsDeviceService& deviceService)
    : httpServer(server), deviceService(deviceService) {
    handleGetDeviceList();
    handleChangeDeviceName();
    handleGetDeviceChannelList();
    handleChannelPanControl();
    handleChannelTiltControl();
    handleChannelLinearControl();
    handleChannelStopControl();
    handleGetChannelPresetList();
    handleSetChannelPreset();
    handleGoToChannelPreset();
    handleUpdateChannelPreset();
    handleDeleteChannelPreset();
    handleGetChannelTourList();
    handleCreateChannelTour();
    handleGetChannelTourInfo();
    handleStartChannelTour();
    handleStopChannelTour();
    handleUpdateChannelTourInfo();
    handleDeleteChannelTour();
}

VmsDeviceController::~VmsDeviceController() {}

// GET /devices
// 장비 목록 조회 API. paging query를 검증한 뒤 DB의 장비 목록을 반환한다.
void VmsDeviceController::handleGetDeviceList() {
httpServer.addRoute(HttpMethod::GET, "/devices", [this](const Http::Request& req) -> Http::Response {
    int page = 1;
    int size = 10;

    if (const auto* v = req.getQueryParam("page")) {
        if (!VmsRequest::parsePositiveInt(*v, page)) return VmsApiResponse::invalidArgumentResponse();
    }
    if (const auto* v = req.getQueryParam("size")) {
        if (!VmsRequest::parsePositiveInt(*v, size)) return VmsApiResponse::invalidArgumentResponse();
    }

    const auto result = deviceService.getDeviceList(page, size);
    if (result.status != VmsDeviceService::ListStatus::Success) {
        return VmsApiResponse::internalServerErrorResponse();
    }

    json devices = json::array();
    for (const auto& item : result.items) {
        devices.push_back({
            {"deviceId", item.deviceId},
            {"name", item.name},
            {"type", item.type},
            {"ip", item.ip},
            {"model", item.model},
            {"online", item.online},
            {"health", item.health},
            {"channelCount", item.channelCount}
        });
    }

    return VmsApiResponse::successResponse(devices);
});
}

// POST /devices/{deviceId}
// 장비 이름 변경 API. request body의 name을 검증하고 DB에 반영한다.
void VmsDeviceController::handleChangeDeviceName() {
httpServer.addRoute(HttpMethod::POST, "/devices/{deviceId}", [this](const Http::Request& req) -> Http::Response {
    std::int64_t deviceId = 0;
    if (!VmsRequest::parsePathInt64(req, "deviceId", deviceId)) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto requestBody = VmsRequest::parseJsonObject(req);
    if (!requestBody
        || !requestBody->contains("name")
        || !(*requestBody)["name"].is_string()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto result = deviceService.changeDeviceName(deviceId, (*requestBody)["name"].get<std::string>());
    if (result.status == VmsDeviceService::MutationStatus::InvalidArgument) {
        return VmsApiResponse::invalidArgumentResponse();
    }
    if (result.status == VmsDeviceService::MutationStatus::NotFound) {
        return VmsApiResponse::notFoundResponse();
    }
    if (result.status != VmsDeviceService::MutationStatus::Success) {
        return VmsApiResponse::internalServerErrorResponse();
    }

    return VmsApiResponse::successResponse({
        {"deviceId", result.deviceId},
        {"name", result.name}
    });
});
}

// GET /devices/{deviceId}/channels
// 특정 장비에 속한 채널 목록 조회 API. channels/profiles 테이블 기준으로 반환한다.
void VmsDeviceController::handleGetDeviceChannelList() {
httpServer.addRoute(HttpMethod::GET, "/devices/{deviceId}/channels", [this](const Http::Request& req) -> Http::Response {
    std::int64_t deviceId = 0;
    if (!VmsRequest::parsePathInt64(req, "deviceId", deviceId)) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto result = deviceService.getDeviceChannelList(deviceId);
    if (result.status == VmsDeviceService::ListStatus::NotFound) {
        return VmsApiResponse::notFoundResponse();
    }
    if (result.status != VmsDeviceService::ListStatus::Success) {
        return VmsApiResponse::internalServerErrorResponse();
    }

    json channels = json::array();
    for (const auto& item : result.items) {
        channels.push_back({
            {"channelId", item.channelId},
            {"channelNo", item.channelNo},
            {"name", item.name},
            {"videoCodec", item.videoCodec},
            {"audioCodec", item.audioCodec},
            {"rtsp", item.rtsp}
        });
    }

    return VmsApiResponse::successResponse(channels);
});
}

// POST /channels/{channelId}/ptz/pan
// 채널의 pan 연속 이동 API. channelId로 PTZ target을 찾아 ContinuousMove를 호출한다.
void VmsDeviceController::handleChannelPanControl() {
httpServer.addRoute(HttpMethod::POST, "/channels/{channelId}/ptz/pan", [this](const Http::Request& req) -> Http::Response {
    std::int64_t channelId = 0;
    if (!VmsRequest::parsePathInt64(req, "channelId", channelId)) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto requestBody = VmsRequest::parseJsonObject(req);
    if (!requestBody) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto speed = jsonPtzVelocityValue(*requestBody);
    if (!speed) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto result = deviceService.panChannel(channelId, *speed);
    return ptzControlResponse(result.status);
});
}

// POST /channels/{channelId}/ptz/tilt
// 채널의 tilt 연속 이동 API. channelId로 PTZ target을 찾아 ContinuousMove를 호출한다.
void VmsDeviceController::handleChannelTiltControl() {
httpServer.addRoute(HttpMethod::POST, "/channels/{channelId}/ptz/tilt", [this](const Http::Request& req) -> Http::Response {
    std::int64_t channelId = 0;
    if (!VmsRequest::parsePathInt64(req, "channelId", channelId)) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto requestBody = VmsRequest::parseJsonObject(req);
    if (!requestBody) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto speed = jsonPtzVelocityValue(*requestBody);
    if (!speed) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto result = deviceService.tiltChannel(channelId, *speed);
    return ptzControlResponse(result.status);
});
}

// POST /channels/{channelId}/ptz/linear
// pan/tilt/zoom을 한 번에 전달하는 PTZ 이동 API다.
void VmsDeviceController::handleChannelLinearControl() {
httpServer.addRoute(HttpMethod::POST, "/channels/{channelId}/ptz/linear", [this](const Http::Request& req) -> Http::Response {
    std::int64_t channelId = 0;
    if (!VmsRequest::parsePathInt64(req, "channelId", channelId)) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto requestBody = VmsRequest::parseJsonObject(req);
    if (!requestBody) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto value = jsonPtzVelocityValue(*requestBody);
    if (!value) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto result = deviceService.moveChannelLinear(channelId, *value, *value, 0.0f);
    return ptzControlResponse(result.status);
});
}

// POST /channels/{channelId}/ptz/stop
// 진행 중인 PTZ 이동 중지 API. channelId로 PTZ target을 찾아 Stop을 호출한다.
void VmsDeviceController::handleChannelStopControl() {
httpServer.addRoute(HttpMethod::POST, "/channels/{channelId}/ptz/stop", [this](const Http::Request& req) -> Http::Response {
    std::int64_t channelId = 0;
    if (!VmsRequest::parsePathInt64(req, "channelId", channelId)) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    bool panTilt = true;
    bool zoom = true;
    if (!trim(req.body).empty()) {
        const auto requestBody = VmsRequest::parseJsonObject(req);
        if (!requestBody) {
            return VmsApiResponse::invalidArgumentResponse();
        }
        if (requestBody->contains("panTilt")) {
            const auto value = jsonBoolField(*requestBody, "panTilt");
            if (!value) return VmsApiResponse::invalidArgumentResponse();
            panTilt = *value;
        }
        if (requestBody->contains("zoom")) {
            const auto value = jsonBoolField(*requestBody, "zoom");
            if (!value) return VmsApiResponse::invalidArgumentResponse();
            zoom = *value;
        }
    }

    const auto result = deviceService.stopChannel(channelId, panTilt, zoom);
    return ptzControlResponse(result.status);
});
}

// GET /channels/{channelId}/ptz/presets
// 채널 PTZ preset 목록 조회 API. DB에 저장된 preset cache를 반환한다.
void VmsDeviceController::handleGetChannelPresetList() {
httpServer.addRoute(HttpMethod::GET, "/channels/{channelId}/ptz/presets", [this](const Http::Request& req) -> Http::Response {
    std::int64_t channelId = 0;
    if (!VmsRequest::parsePathInt64(req, "channelId", channelId)) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto result = deviceService.getChannelPresetList(channelId);
    if (result.status == VmsDeviceService::ListStatus::NotFound) {
        return VmsApiResponse::notFoundResponse();
    }
    if (result.status != VmsDeviceService::ListStatus::Success) {
        return VmsApiResponse::internalServerErrorResponse();
    }

    json items = json::array();
    for (const auto& item : result.items) {
        items.push_back({
            {"presetId", item.presetId},
            {"presetToken", item.presetToken},
            {"presetName", item.presetName},
            {"thumbnailUrl", "/ptz/presets/" + std::to_string(item.presetId) + "/thumbnail"},
            {"pan", item.pan},
            {"tilt", item.tilt},
            {"zoom", item.zoom},
            {"linear", item.linear}
        });
    }

    return VmsApiResponse::successResponse({{"items", items}});
});
}

// POST /channels/{channelId}/ptz/presets
// 현재 PTZ 위치를 preset으로 저장하는 API. ONVIF SetPreset 후 DB cache를 upsert한다.
void VmsDeviceController::handleSetChannelPreset() {
httpServer.addRoute(HttpMethod::POST, "/channels/{channelId}/ptz/presets", [this](const Http::Request& req) -> Http::Response {
    std::int64_t channelId = 0;
    if (!VmsRequest::parsePathInt64(req, "channelId", channelId)) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto requestBody = VmsRequest::parseJsonObject(req);
    if (!requestBody
        || !requestBody->contains("presetName")
        || !(*requestBody)["presetName"].is_string()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto result = deviceService.setChannelPreset(
        channelId,
        (*requestBody)["presetName"].get<std::string>());
    return ptzControlResponse(result.status);
});
}

// POST /channels/{channelId}/ptz/presets/{presetId}/goto
// 저장된 preset 위치로 이동하는 API. VMS presetId를 ONVIF presetToken으로 변환한다.
void VmsDeviceController::handleGoToChannelPreset() {
httpServer.addRoute(HttpMethod::POST, "/channels/{channelId}/ptz/presets/{presetId}/goto", [this](const Http::Request& req) -> Http::Response {
    std::int64_t channelId = 0;
    std::int64_t presetId = 0;
    if (!VmsRequest::parsePathInt64(req, "channelId", channelId)
        || !VmsRequest::parsePathInt64(req, "presetId", presetId)) {
        return VmsApiResponse::invalidArgumentResponse();
    }
    if (!trim(req.body).empty() && !VmsRequest::parseJsonObject(req)) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto result = deviceService.goToChannelPreset(channelId, presetId);
    return ptzControlResponse(result.status);
});
}

// PUT /channels/{channelId}/ptz/presets/{presetId}
// preset 이름을 장비와 DB cache에 반영한다.
void VmsDeviceController::handleUpdateChannelPreset() {
httpServer.addRoute(HttpMethod::PUT, "/channels/{channelId}/ptz/presets/{presetId}", [this](const Http::Request& req) -> Http::Response {
    std::int64_t channelId = 0;
    std::int64_t presetId = 0;
    if (!VmsRequest::parsePathInt64(req, "channelId", channelId)
        || !VmsRequest::parsePathInt64(req, "presetId", presetId)) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto requestBody = VmsRequest::parseJsonObject(req);
    if (!requestBody
        || !requestBody->contains("presetName")
        || !(*requestBody)["presetName"].is_string()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto result = deviceService.updateChannelPreset(
        channelId,
        presetId,
        (*requestBody)["presetName"].get<std::string>());
    return ptzControlResponse(result.status);
});
}

// DELETE /channels/{channelId}/ptz/presets/{presetId}
// preset을 장비와 DB cache에서 삭제한다.
void VmsDeviceController::handleDeleteChannelPreset() {
httpServer.addRoute(HttpMethod::DELETE, "/channels/{channelId}/ptz/presets/{presetId}", [this](const Http::Request& req) -> Http::Response {
    std::int64_t channelId = 0;
    std::int64_t presetId = 0;
    if (!VmsRequest::parsePathInt64(req, "channelId", channelId)
        || !VmsRequest::parsePathInt64(req, "presetId", presetId)) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto result = deviceService.deleteChannelPreset(channelId, presetId);
    return ptzControlResponse(result.status);
});
}

// GET /channels/{channelId}/ptz/tours
// 채널 preset tour 목록 조회 API. 현재는 샘플 응답이며, app-onvif preset tour wrapper는 아직 없다.
void VmsDeviceController::handleGetChannelTourList() {
httpServer.addRoute(HttpMethod::GET, "/channels/{channelId}/ptz/tours", [this](const Http::Request& req) -> Http::Response {
    const std::string* channelNo = req.getPathParam("channelNo");
    
    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "application/json");
    json response_body = {
        "ok", true,
        "data", json::array({
            {
                {"tourId", 1},
                {"tourName", "Tour 1"},
                {"isEnabled", true},
                {"itemCount", 5}
            },
            {
                {"tourId", 2},
                {"tourName", "Tour 2"},
                {"isEnabled", false},
                {"itemCount", 3}
            }
        }),
        "error", nullptr
    };
    res.body = response_body.dump();
    return res;
});
}

// POST /channels/{channelId}/ptz/tours
// preset tour 생성 API. 추후 ONVIF CreatePresetTour 또는 VMS 내부 tour 저장 정책과 연결한다.
void VmsDeviceController::handleCreateChannelTour() {
httpServer.addRoute(HttpMethod::POST, "/channels/{channelId}/ptz/tours", [this](const Http::Request& req) -> Http::Response {
    const std::string* channelNo = req.getPathParam("channelNo");
    
    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "application/json");
    json response_body = {
        "ok", true,
        "data", {
            {"result", "OK"}
        },
        "error", nullptr
    };
    res.body = response_body.dump();
    return res;
});
}

// GET /channels/{channelId}/ptz/tours{tourId}
// preset tour 상세 조회 API. tour item 순서, 체류 시간, 이동 속도 정보를 반환한다.
void VmsDeviceController::handleGetChannelTourInfo() {
httpServer.addRoute(HttpMethod::GET, "/channels/{channelId}/ptz/tours/{tourId}", [this](const Http::Request& req) -> Http::Response {
    const std::string* channelNo = req.getPathParam("channelNo");
    const std::string* tourId = req.getPathParam("tourId");
    
    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "application/json");
    json response_body = {
        "ok", true,
        "data", {
            {"tourId", *tourId},
            {"tourName", "Tour 1"},
            {"repeatMode", "LOOP"},
            {"isEnabled", true},
            {"items", json::array({
                {
                    {"tourItemId", 1},
                    {"presetId", 1},
                    {"presetName", "Preset 1"},
                    {"seqNo", 1},
                    {"staySecond", 5},
                    {"moveSpeed", 50}
                },
                {
                    {"tourItemId", 2},
                    {"presetId", 2},
                    {"presetName", "Preset 2"},
                    {"seqNo", 2},
                    {"staySecond", 10},
                    {"moveSpeed", 50}
                }
            })}
        },
        "error", nullptr
    };
    res.body = response_body.dump();
    return res;
});
}

// POST /channels/{channelId}/ptz/tours/{tourId}/start
// preset tour 시작 API. 추후 ONVIF OperatePresetTour Start와 연결한다.
void VmsDeviceController::handleStartChannelTour() {
httpServer.addRoute(HttpMethod::POST, "/channels/{channelId}/ptz/tours/{tourId}/start", [this](const Http::Request& req) -> Http::Response {
    const std::string* channelNo = req.getPathParam("channelNo");
    const std::string* tourId = req.getPathParam("tourId");
    
    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "application/json");
    json response_body = {
        "ok", true,
        "data", {
            {"result", "OK"}
        },
        "error", nullptr
    };
    res.body = response_body.dump();
    return res;
});
}

// POST /channels/{channelId}/ptz/tours/{tourId}/stop
// preset tour 중지 API. 추후 ONVIF OperatePresetTour Stop과 연결한다.
void VmsDeviceController::handleStopChannelTour() {
httpServer.addRoute(HttpMethod::POST, "/channels/{channelId}/ptz/tours/{tourId}/stop", [this](const Http::Request& req) -> Http::Response {
    const std::string* channelNo = req.getPathParam("channelNo");
    const std::string* tourId = req.getPathParam("tourId");
    
    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "application/json");
    json response_body = {
        "ok", true,
        "data", {
            {"result", "OK"}
        },
        "error", nullptr
    };
    res.body = response_body.dump();
    return res;
});
}

// PUT /channels/{channelId}/ptz/tours/{tourId}
// preset tour 이름/반복 조건/item 구성 갱신 API.
void VmsDeviceController::handleUpdateChannelTourInfo() {
httpServer.addRoute(HttpMethod::PUT, "/channels/{channelId}/ptz/tours/{tourId}", [this](const Http::Request& req) -> Http::Response {
    const std::string* channelNo = req.getPathParam("channelNo");
    const std::string* tourId = req.getPathParam("tourId");
    
    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "application/json");
    json response_body = {
        "ok", true,
        "data", {
            {"result", "OK"}
        },
        "error", nullptr
    };
    res.body = response_body.dump();
    return res;
});
}

// DELETE /channels/{channelId}/ptz/tours/{tourId}
// preset tour 삭제 API. 추후 ONVIF RemovePresetTour 또는 VMS DB 삭제와 연결한다.
void VmsDeviceController::handleDeleteChannelTour() {
httpServer.addRoute(HttpMethod::DELETE, "/channels/{channelId}/ptz/tours/{tourId}", [this](const Http::Request& req) -> Http::Response {
    const std::string* channelNo = req.getPathParam("channelNo");
    const std::string* tourId = req.getPathParam("tourId");
    
    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "application/json");
    json response_body = {
        "ok", true,
        "data", {
            {"result", "OK"}
        },
        "error", nullptr
    };
    res.body = response_body.dump();
    return res;
});
}
