#ifndef __VMSREQUESTHELPERS_H__
#define __VMSREQUESTHELPERS_H__

#include "HttpServer.h"
#include "StringHelpers.h"

#include "nlohmann/json.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace VmsRequest {

inline std::optional<std::string> extractBearerToken(const Http::Request& req) {
    const std::string* authorization = req.headers.get("authorization");
    if (authorization == nullptr) return std::nullopt;

    const std::string value = trim(*authorization);
    constexpr const char* kPrefix = "Bearer ";
    if (!starts_with_case_insensitive(value, kPrefix)) return std::nullopt;

    const std::string token = trim(value.substr(7));
    if (token.empty()) return std::nullopt;
    return token;
}

inline std::optional<std::int64_t> authenticatedUserId(const Http::Request& req) {
    if (!req.auth.authenticated || req.auth.userId <= 0) return std::nullopt;
    return req.auth.userId;
}

inline std::optional<nlohmann::json> parseJsonObject(const Http::Request& req) {
    try {
        nlohmann::json body = nlohmann::json::parse(req.body);
        if (!body.is_object()) return std::nullopt;
        return body;
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

inline bool parsePathInt64(const Http::Request& req, const std::string& key, std::int64_t& out) {
    const std::string* value = req.getPathParam(key);
    return value != nullptr && parse_int64_strict(*value, out) && out > 0;
}

inline bool parsePositiveInt(const std::string& value, int& out, int maxValue = 1000000) {
    std::int64_t parsed = 0;
    if (!parse_int64_strict(value, parsed) || parsed <= 0 || parsed > maxValue) return false;
    out = static_cast<int>(parsed);
    return true;
}

} // namespace VmsRequest

#endif // __VMSREQUESTHELPERS_H__
