#include "VmsDeviceController.h"

VmsDeviceController::VmsDeviceController(HttpServer& server) : httpServer(server) {}

VmsDeviceController::~VmsDeviceController() {}

void VmsDeviceController::handleGetDeviceList() {
httpServer.addRoute(HttpMethod::GET, "/devices", [this](const Http::Request& req) -> Http::Response {   
    int page = 1;
    int size = 10;

    if (const auto* v = req.getQueryParam("page")) { page = std::stoi(*v); }

    if (const auto* v = req.getQueryParam("size")) { size = std::stoi(*v); }

    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "application/json");
    json response_body = {
        "ok", true,
        "data", json::array({
            {
                {"deviceId", "11"},
                {"name", "Camera 1"},
                {"type", "CCTV"},
                {"ip", "192.168.1.101"},
                {"model", "Model X"},
                {"online", true},
                {"health", "DOWN"},
                {"channelCount", 4}
            },
            {
                {"deviceId", "12"},
                {"name", "Camera 2"},
                {"type", "CCTV"},
                {"ip", "192.168.1.102"},
                {"model", "Model Y"},
                {"online", false},
                {"health", "DOWN"},
                {"channelCount", 4}
            }
        }),
        "error", nullptr
    };
    res.body = response_body.dump();
    return res;
});
}

void VmsDeviceController::handleChangeDeviceName() {
httpServer.addRoute(HttpMethod::POST, "/devices/{deviceId}", [this](const Http::Request& req) -> Http::Response {
    const std::string* deviceId = req.getPathParam("deviceId");
    
    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "application/json");
    json response_body = {
        "ok", true,
        "data", {
            {"deviceId", *deviceId},
            {"name", "newName"},
            {"updatedAt", "2024-06-01T12:00:00Z"}
        },
        "error", nullptr
    };
    res.body = response_body.dump();
    return res;
});
}

void VmsDeviceController::handleGetDeviceChannelList() {
httpServer.addRoute(HttpMethod::GET, "/devices/{deviceId}/channels", [this](const Http::Request& req) -> Http::Response {
    const std::string* deviceId = req.getPathParam("deviceId");
    
    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "application/json");
    json response_body = {
        "ok", true,
        "data", json::array({
            {
                {"channelId", 101},
                {"channelNo", 0},
                {"name", "CH1"},
                {"videoCodec", "H264"},
                {"audioCodec", "AAC"},
                {"rtsp", "rtsp://192.168.0.69:554/0/onvif/profile1/media.smp"}
            },
            {
                {"channelId", 102},
                {"channelNo", 1},
                {"name", "CH2"},
                {"videoCodec", "H264"},
                {"audioCodec", "AAC"},
                {"rtsp", "rtsp://192.168.0.69:554/0/onvif/profile1/media.smp"}
            }
        }),
        "error", nullptr
    };
    res.body = response_body.dump();
    return res;
});
}

void VmsDeviceController::handleChannelPanControl() {
httpServer.addRoute(HttpMethod::POST, "/channels/{channelId}/ptz/pan", [this](const Http::Request& req) -> Http::Response {
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

void VmsDeviceController::handleChannelTiltControl() {
httpServer.addRoute(HttpMethod::POST, "/channels/{channelId}/ptz/tilt", [this](const Http::Request& req) -> Http::Response {
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

void VmsDeviceController::handleChannelLinearControl() {
httpServer.addRoute(HttpMethod::POST, "/channels/{channelId}/ptz/linear", [this](const Http::Request& req) -> Http::Response {
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

void VmsDeviceController::handleChannelStopControl() {
httpServer.addRoute(HttpMethod::POST, "/channels/{channelId}/ptz/stop", [this](const Http::Request& req) -> Http::Response {
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

void VmsDeviceController::handleGetChannelPresetList() {
httpServer.addRoute(HttpMethod::GET, "/channels/{channelId}/ptz/presets", [this](const Http::Request& req) -> Http::Response {
    const std::string* channelNo = req.getPathParam("channelNo");
    
    // TODO 아래는 예시
    Http::Response res;
    res.setStatus(HttpCode::OK);
    res.headers.set("Content-Type", "application/json");
    json response_body = {
        "ok", true,
        "data", json::array({
            {
                {"presetId", 1},
                {"presetToken", "Preset001"},
                {"presetName", "정문"},
                {"thumbnailUrl", "/ptz/presets/1/thumbnail"},
                {"pan", 10},
                {"tilt", 20},
                {"zoom", 30},
                {"linear", "40"}
            },
            {
                {"presetId", 1002},
                {"presetToken", "Preset002"},
                {"presetName", "후문"},
                {"thumbnailUrl", "/ptz/presets/1002/thumbnail"},
                {"pan", 15},
                {"tilt", 25},
                {"zoom", 35},
                {"linear", "45"}
            }
        }),
        "error", nullptr
    };
    res.body = response_body.dump();
    return res;
});
}

void VmsDeviceController::handleSetChannelPreset() {
httpServer.addRoute(HttpMethod::POST, "/channels/{channelId}/ptz/presets", [this](const Http::Request& req) -> Http::Response {
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

void VmsDeviceController::handleGoToChannelPreset() {
httpServer.addRoute(HttpMethod::POST, "/channels/{channelId}/ptz/presets/{presetId}/goto", [this](const Http::Request& req) -> Http::Response {
    const std::string* channelNo = req.getPathParam("channelNo");
    const std::string* presetId = req.getPathParam("presetId");
    
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

void VmsDeviceController::handleUpdateChannelPreset() {
httpServer.addRoute(HttpMethod::PUT, "/channels/{channelId}/ptz/presets/{presetId}", [this](const Http::Request& req) -> Http::Response {
    const std::string* channelNo = req.getPathParam("channelNo");
    const std::string* presetId = req.getPathParam("presetId");
    
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

void VmsDeviceController::handleDeleteChannelPreset() {
httpServer.addRoute(HttpMethod::DELETE, "/channels/{channelId}/ptz/presets/{presetId}", [this](const Http::Request& req) -> Http::Response {
    const std::string* channelNo = req.getPathParam("channelNo");
    const std::string* presetId = req.getPathParam("presetId");
    
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
                {"presetCount", 5}
            },
            {
                {"tourId", 2},
                {"tourName", "Tour 2"},
                {"presetCount", 3}
            }
        }),
        "error", nullptr
    };
    res.body = response_body.dump();
    return res;
}

void VmsDeviceController::handleCreateChannelTour() {

}

void VmsDeviceController::handleGetChannelTourInfo() {

}

void VmsDeviceController::handleStartChannelTour() {

}

void VmsDeviceController::handleStopChannelTour() {

}

void VmsDeviceController::handleUpdateChannelTourInfo() {

}

void VmsDeviceController::handleDeleteChannelTour() {

}
