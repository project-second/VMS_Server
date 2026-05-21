#include "VmsUserController.h"

#include "VmsApiResponses.h"

VmsUserController::VmsUserController(HttpServer& server, VmsUserService& userService) : httpServer(server), userService(userService) {}

VmsUserController::~VmsUserController() {
    handleUserLogin();
    handleUserLogout();
    handleUserSignup();
    handleGetUserInfo();
    handleUpdateUserInfo();
    handleDeleteUser();
}

void VmsUserController::handleUserLogin() {
httpServer.addRoute(HttpMethod::POST, "/user/login", [this](const Http::Request& req) -> Http::Response {
    json requestBody;
    try {
        requestBody = json::parse(req.body);
    } catch (const json::parse_error& e) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    if (!requestBody.is_object()
        || !requestBody.contains("id") || !requestBody["id"].is_string()
        || !requestBody.contains("pw") || !requestBody["pw"].is_string()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const std::string& loginId = requestBody["id"].get<std::string>();
    const std::string& password = requestBody["pw"].get<std::string>();
    if (trim(loginId).empty() || trim(password).empty()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    // TODO Implementation for user login

    // return VmsApiResponse::successResponse({
    //     {"accessToken", result.accessToken},
    //     {"expiresIn", result.expiresIn},
    //     {"issuedAt", result.issuedAt}
    // });
});
}

void VmsUserController::handleUserLogout() {
httpServer.addRoute(HttpMethod::POST, "/user/logout", [this](const Http::Request& req) -> Http::Response {
    // TODO Implementation for user logout

    return VmsApiResponse::successResponseWithoutData();
});
}

void VmsUserController::handleUserSignup() {
httpServer.addRoute(HttpMethod::POST, "/user/signup", [this](const Http::Request& req) -> Http::Response {
    // TODO Implementation for user signup

    return VmsApiResponse::successResponseWithoutData();
});
}

void VmsUserController::handleGetUserInfo() {
httpServer.addRoute(HttpMethod::GET, "/user/info", [this](const Http::Request& req) -> Http::Response {
    // TODO Implementation for getting user info
});
}

void VmsUserController::handleUpdateUserInfo() {
httpServer.addRoute(HttpMethod::PATCH, "/user/info", [this](const Http::Request& req) -> Http::Response {
    // TODO Implementation for updating user info
});
}

void VmsUserController::handleDeleteUser() {
httpServer.addRoute(HttpMethod::DELETE, "/user", [this](const Http::Request& req) -> Http::Response {
    // TODO Implementation for deleting user
});
}

void VmsUserController::handleRefreshToken() {
httpServer.addRoute(HttpMethod::POST, "/users/token/refresh", [this](const Http::Request& req) -> Http::Response {
    // TODO Implementation for refreshing token
});
}