#include "VmsAdminController.h"

#include "VmsApiResponses.h"

VmsAdminController::VmsAdminController(HttpServer& server) : httpServer(server) {
    handleGetUserApprovals();
    handleApproveUser();
    handleRejectUser();
    handleGetUsersRole();
    handleAddUserRole();
    handleDeleteUserRole();
    handleUserDisactivation();
}

VmsAdminController::~VmsAdminController() {}

void VmsAdminController::handleGetUserApprovals() {
httpServer.addRoute(HttpMethod::GET, "/admin/users/approvals", [this](const Http::Request& req) -> Http::Response {
    int page = 1;
    int size = 10;
    std::string status;

    if (const auto* v = req.getQueryParam("page")) { page = std::stoi(*v); }

    if (const auto* v = req.getQueryParam("size")) { size = std::stoi(*v); }

    if (const auto* v = req.getQueryParam("status")) { status = *v; }

    // if (!status.empty()) {
    //     // WHERE status = :status
    // } else {
    //     // status 조건 없이 조회
    // }

    // TODO Implementation for getting user approvals

    return VmsApiResponse::successResponseWithoutData();
});
}

void VmsAdminController::handleApproveUser() {
httpServer.addRoute(HttpMethod::POST, "/admin/users/approvals/{approvalId}/approve", [this](const Http::Request& req) -> Http::Response {
    const std::string* approvalId = req.getPathParam("approvalId"); // 승인 ID 가져오기
    // TODO Implementation for approving user

    return VmsApiResponse::successResponseWithoutData();
});
}

void VmsAdminController::handleRejectUser() {
httpServer.addRoute(HttpMethod::POST, "/admin/users/approvals/{approvalId}/reject", [this](const Http::Request& req) -> Http::Response {
    const std::string* approvalId = req.getPathParam("approvalId"); // 승인 ID 가져오기
    // TODO Implementation for rejecting user

    return VmsApiResponse::successResponseWithoutData();
});
}

void VmsAdminController::handleGetUsersRole() {
httpServer.addRoute(HttpMethod::GET, "/admin/users/{userId}/roles", [this](const Http::Request& req) -> Http::Response {
    const std::string* userId = req.getPathParam("userId"); // 사용자 ID 가져오기
    // TODO Implementation for getting user's roles

    return VmsApiResponse::successResponseWithoutData();
});
}

void VmsAdminController::handleAddUserRole() {
httpServer.addRoute(HttpMethod::POST, "/admin/users/{userId}/roles", [this](const Http::Request& req) -> Http::Response {
    const std::string* userId = req.getPathParam("userId"); // 사용자 ID 가져오기
    // TODO Implementation for adding role to user

    return VmsApiResponse::successResponseWithoutData();
});
}

void VmsAdminController::handleDeleteUserRole() {
httpServer.addRoute(HttpMethod::DELETE, "/admin/users/{userId}/roles", [this](const Http::Request& req) -> Http::Response {
    const std::string* userId = req.getPathParam("userId"); // 사용자 ID 가져오기
    // TODO Implementation for deleting role from user

    return VmsApiResponse::successResponseWithoutData();
});
}

void VmsAdminController::handleUserDisactivation() {
httpServer.addRoute(HttpMethod::PATCH, "/admin/users/{userId}/status", [this](const Http::Request& req) -> Http::Response {
    const std::string* userId = req.getPathParam("userId"); // 사용자 ID 가져오기
    // TODO Implementation for disactivating user

    return VmsApiResponse::successResponseWithoutData();
});
}
