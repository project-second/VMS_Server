#include "app/VmsAuthMiddleware.h"

#include "StringHelpers.h"
#include "VmsApiResponses.h"
#include "VmsRequestHelpers.h"

namespace {

std::string requestPath(const Http::Request& req) {
    return split_once(req.target, '?')[0];
}

bool requestMatches(const Http::Request& req, const std::string& method, const std::string& path) {
    return to_lower(req.method) == to_lower(method) && requestPath(req) == path;
}

bool isPublicHttpApi(const Http::Request& req) {
    return requestMatches(req, "POST", "/users/login")
        || requestMatches(req, "POST", "/users/signup")
        || requestMatches(req, "POST", "/users/token/refresh");
}

bool isAdminHttpApi(const Http::Request& req) {
    return requestPath(req).rfind("/admin/", 0) == 0;
}

} // namespace

VmsAuthMiddleware::VmsAuthMiddleware(VmsUserService& userService, VmsAdminService& adminService)
    : userService_(userService), adminService_(adminService) {}

std::optional<Http::Response> VmsAuthMiddleware::operator()(Http::Request& req) const {
    if (isPublicHttpApi(req)) {
        return std::nullopt;
    }

    const auto accessToken = VmsRequest::extractBearerToken(req);
    if (!accessToken) {
        return VmsApiResponse::invalidTokenResponse();
    }

    const auto validation = userService_.validateAccessToken(*accessToken);
    if (validation.status != VmsUserService::TokenStatus::Valid) {
        return VmsApiResponse::invalidTokenResponse();
    }

    req.auth.authenticated = true;
    req.auth.userId = validation.userId;

    if (isAdminHttpApi(req)) {
        const auto adminStatus = adminService_.checkAdmin(validation.userId);
        if (adminStatus == VmsAdminService::AdminCheckStatus::Forbidden) {
            return VmsApiResponse::forbiddenResponse();
        }
        if (adminStatus == VmsAdminService::AdminCheckStatus::NotFound) {
            return VmsApiResponse::invalidTokenResponse();
        }
        if (adminStatus != VmsAdminService::AdminCheckStatus::Authorized) {
            return VmsApiResponse::internalServerErrorResponse();
        }
        req.auth.admin = true;
    }

    return std::nullopt;
}
