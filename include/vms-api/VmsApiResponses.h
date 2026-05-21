#ifndef __VMS_API_RESPONSES_H__
#define __VMS_API_RESPONSES_H__

#include "HttpServer.h"

#include "nlohmann/json.hpp"

namespace VmsApiResponse {

inline Http::Response jsonResponse(HttpCode code, const nlohmann::json& body) {
    Http::Response res;
    res.setStatus(code);
    res.headers.set("Content-Type", "application/json");
    res.body = body.dump();
    return res;
}

inline Http::Response successResponse(const nlohmann::json& data = nullptr, HttpCode code = HttpCode::OK) {
    return jsonResponse(code, {
        {"ok", true},
        {"data", data},
        {"error", nullptr}
    });
}

inline Http::Response successResponseWithoutData(HttpCode code = HttpCode::OK) {
    return jsonResponse(code, {
        {"ok", true},
        {"error", nullptr}
    });
}

inline Http::Response errorResponse(
    HttpCode code, const char* error_code, bool ok = false, bool include_data = true) {
    nlohmann::json body = {
        {"ok", ok},
        {"error", {{"code", error_code}}}
    };
    if (include_data) {
        body["data"] = nullptr;
    }
    return jsonResponse(code, body);
}

inline Http::Response invalidTokenResponse(bool ok = false, bool include_data = true) {
    return errorResponse(HttpCode::Unauthorized, "AUTH_INVALID_TOKEN", ok, include_data);
}

inline Http::Response invalidArgumentResponse() {
    return errorResponse(HttpCode::Bad_Request, "INVALID_ARGUMENT");
}

inline Http::Response internalServerErrorResponse() {
    return errorResponse(HttpCode::Internal_Server_Error, "INTERNAL_SERVER_ERROR");
}

inline Http::Response notFoundResponse() {
    return errorResponse(HttpCode::Not_Found, "NOT_FOUND");
}

} // namespace VmsApiResponse

#endif // __VMS_API_RESPONSES_H__
