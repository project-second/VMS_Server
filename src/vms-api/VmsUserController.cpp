#include "VmsUserController.h"

#include "VmsApiResponses.h"
#include "VmsRequestHelpers.h"
#include "StringHelpers.h"

namespace {

Http::Response invalidCredentialsResponse() {
    return VmsApiResponse::jsonResponse(HttpCode::Unauthorized, {
        {"ok", false},
        {"data", nullptr},
        {"error", {{"code", "AUTH_INVALID_CREDENTIALS"}}}
    });
}

Http::Response pendingApprovalResponse() {
    return VmsApiResponse::jsonResponse(HttpCode::Forbidden, {
        {"ok", false},
        {"data", nullptr},
        {"error", {
            {"code", "AUTH_ACCOUNT_PENDING_APPROVAL"},
            {"message", "Account is pending administrator approval"}
        }}
    });
}

Http::Response rejectedAccountResponse() {
    return VmsApiResponse::jsonResponse(HttpCode::Forbidden, {
        {"ok", false},
        {"data", nullptr},
        {"error", {
            {"code", "AUTH_ACCOUNT_REJECTED"},
            {"message", "Account was rejected by administrator"}
        }}
    });
}

Http::Response inactiveAccountResponse() {
    return VmsApiResponse::jsonResponse(HttpCode::Forbidden, {
        {"ok", false},
        {"data", nullptr},
        {"error", {
            {"code", "AUTH_ACCOUNT_INACTIVE"},
            {"message", "Account is inactive"}
        }}
    });
}

Http::Response invalidRefreshTokenResponse() {
    return VmsApiResponse::jsonResponse(HttpCode::Unauthorized, {
        {"ok", false},
        {"data", nullptr},
        {"error", {{"code", "AUTH_INVALID_REFRESH_TOKEN"}}}
    });
}

Http::Response invalidPasswordResponse() {
    return VmsApiResponse::jsonResponse(HttpCode::Unauthorized, {
        {"ok", false},
        {"data", nullptr},
        {"error", {{"code", "AUTH_INVALID_PASSWORD"}}}
    });
}

} // namespace

VmsUserController::VmsUserController(HttpServer& server, VmsUserService& userService)
    : httpServer(server), userService(userService) {
    handleUserLogin();
    handleUserLogout();
    handleUserSignup();
    handleGetUserInfo();
    handleUpdateUserInfo();
    handleDeleteUser();
    handleRefreshToken();
}

VmsUserController::~VmsUserController() {}

void VmsUserController::handleUserLogin() {
httpServer.addRoute(HttpMethod::POST, "/users/login", [this](const Http::Request& req) -> Http::Response {
    const auto requestBody = VmsRequest::parseJsonObject(req);
    if (!requestBody
        || !requestBody->contains("id") || !(*requestBody)["id"].is_string()
        || !requestBody->contains("pw") || !(*requestBody)["pw"].is_string()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const std::string loginId = (*requestBody)["id"].get<std::string>();
    const std::string password = (*requestBody)["pw"].get<std::string>();
    if (trim(loginId).empty() || trim(password).empty()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto result = userService.login(loginId, password);
    if (result.status == VmsUserService::LoginStatus::InvalidCredentials) {
        return invalidCredentialsResponse();
    }
    if (result.status == VmsUserService::LoginStatus::PendingApproval) {
        return pendingApprovalResponse();
    }
    if (result.status == VmsUserService::LoginStatus::Rejected) {
        return rejectedAccountResponse();
    }
    if (result.status == VmsUserService::LoginStatus::Inactive) {
        return inactiveAccountResponse();
    }
    if (result.status != VmsUserService::LoginStatus::Success) {
        return VmsApiResponse::internalServerErrorResponse();
    }

    return VmsApiResponse::successResponse({
        {"accessToken", result.accessToken},
        {"refreshToken", result.refreshToken},
        {"expiresIn", result.expiresIn},
        {"refreshExpiresIn", result.refreshExpiresIn},
        {"issuedAt", result.issuedAt}
    });
});
}

void VmsUserController::handleUserLogout() {
httpServer.addRoute(HttpMethod::POST, "/users/logout", [this](const Http::Request& req) -> Http::Response {
    const auto accessToken = VmsRequest::extractBearerToken(req);
    const auto requestBody = VmsRequest::parseJsonObject(req);
    if (!accessToken
        || !requestBody
        || !requestBody->contains("refreshToken")
        || !(*requestBody)["refreshToken"].is_string()) {
        return VmsApiResponse::invalidTokenResponse(true, false);
    }

    const std::string refreshToken = (*requestBody)["refreshToken"].get<std::string>();
    if (trim(refreshToken).empty()) {
        return VmsApiResponse::invalidTokenResponse(true, false);
    }

    const auto status = userService.logout(*accessToken, refreshToken);
    if (status != VmsUserService::LogoutStatus::Success) {
        return VmsApiResponse::invalidTokenResponse(true, false);
    }

    return VmsApiResponse::successResponseWithoutData();
});
}

void VmsUserController::handleUserSignup() {
httpServer.addRoute(HttpMethod::POST, "/users/signup", [this](const Http::Request& req) -> Http::Response {
    const auto requestBody = VmsRequest::parseJsonObject(req);
    if (!requestBody
        || !requestBody->contains("id") || !(*requestBody)["id"].is_string()
        || !requestBody->contains("name") || !(*requestBody)["name"].is_string()
        || !requestBody->contains("pw") || !(*requestBody)["pw"].is_string()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const std::string loginId = (*requestBody)["id"].get<std::string>();
    const std::string name = (*requestBody)["name"].get<std::string>();
    const std::string password = (*requestBody)["pw"].get<std::string>();
    if (trim(loginId).empty() || trim(name).empty() || trim(password).empty()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto status = userService.signup(loginId, name, password);
    if (status == VmsUserService::SignupStatus::LoginIdAlreadyExists) {
        return VmsApiResponse::jsonResponse(HttpCode::Conflict, {
            {"ok", false},
            {"data", nullptr},
            {"error", {{"code", "AUTH_LOGINID_ALREADY_EXISTS"}}}
        });
    }
    if (status != VmsUserService::SignupStatus::Created) {
        return VmsApiResponse::internalServerErrorResponse();
    }

    return VmsApiResponse::successResponseWithoutData(HttpCode::Created);
});
}

void VmsUserController::handleGetUserInfo() {
httpServer.addRoute(HttpMethod::GET, "/users/me", [this](const Http::Request& req) -> Http::Response {
    const auto userId = VmsRequest::authenticatedUserId(req);
    if (!userId) {
        return VmsApiResponse::invalidTokenResponse();
    }

    const auto result = userService.getUserInfo(*userId);
    if (result.status == VmsUserService::UserInfoStatus::NotFound) {
        return VmsApiResponse::notFoundResponse();
    }
    if (result.status != VmsUserService::UserInfoStatus::Success) {
        return VmsApiResponse::internalServerErrorResponse();
    }

    return VmsApiResponse::successResponse({
        {"userId", result.userId},
        {"loginId", result.loginId},
        {"name", result.name},
        {"roles", result.roles},
        {"createdAt", result.createdAt},
        {"lastLoginAt", result.lastLoginAt.empty() ? json(nullptr) : json(result.lastLoginAt)}
    });
});
}

void VmsUserController::handleUpdateUserInfo() {
httpServer.addRoute(HttpMethod::PATCH, "/users/me", [this](const Http::Request& req) -> Http::Response {
    const auto userId = VmsRequest::authenticatedUserId(req);
    if (!userId) {
        return VmsApiResponse::invalidTokenResponse();
    }

    const auto requestBody = VmsRequest::parseJsonObject(req);
    if (!requestBody
        || !requestBody->contains("currentPassword")
        || !(*requestBody)["currentPassword"].is_string()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const std::string currentPassword = (*requestBody)["currentPassword"].get<std::string>();
    if (trim(currentPassword).empty()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    std::string name;
    std::string newPassword;
    if (requestBody->contains("name")) {
        if (!(*requestBody)["name"].is_string() || trim((*requestBody)["name"].get<std::string>()).empty()) {
            return VmsApiResponse::invalidArgumentResponse();
        }
        name = (*requestBody)["name"].get<std::string>();
    }
    if (requestBody->contains("newPassword")) {
        if (!(*requestBody)["newPassword"].is_string()
            || trim((*requestBody)["newPassword"].get<std::string>()).empty()) {
            return VmsApiResponse::invalidArgumentResponse();
        }
        newPassword = (*requestBody)["newPassword"].get<std::string>();
    }
    if (name.empty() && newPassword.empty()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto result = userService.updateUserInfo(*userId, currentPassword, name, newPassword);
    if (result.status == VmsUserService::UpdateStatus::InvalidPassword) {
        return invalidPasswordResponse();
    }
    if (result.status == VmsUserService::UpdateStatus::NotFound) {
        return VmsApiResponse::notFoundResponse();
    }
    if (result.status != VmsUserService::UpdateStatus::Success) {
        return VmsApiResponse::internalServerErrorResponse();
    }

    return VmsApiResponse::successResponse({
        {"userId", result.userId},
        {"loginId", result.loginId},
        {"name", result.name},
        {"updatedAt", result.updatedAt}
    });
});
}

void VmsUserController::handleDeleteUser() {
httpServer.addRoute(HttpMethod::DELETE, "/users/me", [this](const Http::Request& req) -> Http::Response {
    const auto userId = VmsRequest::authenticatedUserId(req);
    if (!userId) {
        return VmsApiResponse::invalidTokenResponse(true, false);
    }

    const auto requestBody = VmsRequest::parseJsonObject(req);
    if (!requestBody
        || !requestBody->contains("currentPassword")
        || !(*requestBody)["currentPassword"].is_string()
        || trim((*requestBody)["currentPassword"].get<std::string>()).empty()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto status = userService.deleteUser(*userId, (*requestBody)["currentPassword"].get<std::string>());
    if (status == VmsUserService::DeleteStatus::InvalidPassword) {
        return invalidPasswordResponse();
    }
    if (status == VmsUserService::DeleteStatus::NotFound) {
        return VmsApiResponse::notFoundResponse();
    }
    if (status != VmsUserService::DeleteStatus::Success) {
        return VmsApiResponse::internalServerErrorResponse();
    }

    return VmsApiResponse::successResponseWithoutData();
});
}

void VmsUserController::handleRefreshToken() {
httpServer.addRoute(HttpMethod::POST, "/users/token/refresh", [this](const Http::Request& req) -> Http::Response {
    const auto requestBody = VmsRequest::parseJsonObject(req);
    if (!requestBody
        || !requestBody->contains("refreshToken")
        || !(*requestBody)["refreshToken"].is_string()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const std::string refreshToken = (*requestBody)["refreshToken"].get<std::string>();
    if (trim(refreshToken).empty()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto result = userService.refreshToken(refreshToken);
    if (result.status == VmsUserService::RefreshStatus::InvalidRefreshToken) {
        return invalidRefreshTokenResponse();
    }
    if (result.status != VmsUserService::RefreshStatus::Success) {
        return VmsApiResponse::internalServerErrorResponse();
    }

    return VmsApiResponse::successResponse({
        {"accessToken", result.accessToken},
        {"refreshToken", result.refreshToken},
        {"tokenType", "Bearer"},
        {"expiresIn", result.expiresIn},
        {"refreshExpiresIn", result.refreshExpiresIn},
        {"issuedAt", result.issuedAt}
    });
});
}
