#include "VmsAdminController.h"

#include "VmsApiResponses.h"
#include "VmsRequestHelpers.h"
#include "StringHelpers.h"

namespace {

Http::Response requireAdmin(const Http::Request& req, std::int64_t& adminUserId) {
    if (!req.auth.authenticated || !req.auth.admin || req.auth.userId <= 0) {
        return VmsApiResponse::invalidTokenResponse();
    }

    adminUserId = req.auth.userId;
    return Http::Response{};
}

Http::Response mutationResponse(VmsAdminService::MutationStatus status) {
    if (status == VmsAdminService::MutationStatus::Success) {
        return VmsApiResponse::successResponse({{"result", "OK"}});
    }
    if (status == VmsAdminService::MutationStatus::NotFound) {
        return VmsApiResponse::notFoundResponse();
    }
    if (status == VmsAdminService::MutationStatus::RoleNotFound) {
        return VmsApiResponse::errorResponse(HttpCode::Not_Found, "ROLE_NOT_FOUND");
    }
    if (status == VmsAdminService::MutationStatus::AlreadyExists) {
        return VmsApiResponse::conflictResponse("ROLE_ALREADY_ASSIGNED");
    }
    if (status == VmsAdminService::MutationStatus::InvalidState) {
        return VmsApiResponse::conflictResponse("INVALID_STATE");
    }
    return VmsApiResponse::internalServerErrorResponse();
}

} // namespace

VmsAdminController::VmsAdminController(HttpServer& server, VmsAdminService& adminService)
    : httpServer(server), adminService(adminService) {
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
    std::int64_t adminUserId = 0;
    Http::Response auth = requireAdmin(req, adminUserId);
    if (auth.status_code != 0) return auth;

    int page = 1;
    int size = 10;
    std::string status = "PENDING";

    if (const auto* v = req.getQueryParam("page")) {
        if (!VmsRequest::parsePositiveInt(*v, page)) return VmsApiResponse::invalidArgumentResponse();
    }
    if (const auto* v = req.getQueryParam("size")) {
        if (!VmsRequest::parsePositiveInt(*v, size)) return VmsApiResponse::invalidArgumentResponse();
    }
    if (const auto* v = req.getQueryParam("status")) {
        status = *v;
    }

    const auto result = adminService.getUserApprovals(page, size, status);
    if (result.status != VmsAdminService::ListStatus::Success) {
        return VmsApiResponse::internalServerErrorResponse();
    }

    json items = json::array();
    for (const auto& item : result.items) {
        items.push_back({
            {"approvalId", item.approvalId},
            {"userId", item.userId},
            {"loginId", item.loginId},
            {"name", item.name},
            {"status", item.status},
            {"requestedAt", item.requestedAt}
        });
    }

    return VmsApiResponse::successResponse({
        {"page", result.page},
        {"size", result.size},
        {"totalCount", result.totalCount},
        {"items", items}
    });
});
}

void VmsAdminController::handleApproveUser() {
httpServer.addRoute(HttpMethod::POST, "/admin/users/approvals/{approvalId}/approve", [this](const Http::Request& req) -> Http::Response {
    std::int64_t adminUserId = 0;
    Http::Response auth = requireAdmin(req, adminUserId);
    if (auth.status_code != 0) return auth;

    std::int64_t approvalId = 0;
    if (!VmsRequest::parsePathInt64(req, "approvalId", approvalId)) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto requestBody = VmsRequest::parseJsonObject(req);
    if (!requestBody || !requestBody->contains("roleCodes") || !(*requestBody)["roleCodes"].is_array()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    std::vector<std::string> roleCodes;
    for (const auto& role : (*requestBody)["roleCodes"]) {
        if (!role.is_string() || trim(role.get<std::string>()).empty()) {
            return VmsApiResponse::invalidArgumentResponse();
        }
        roleCodes.emplace_back(role.get<std::string>());
    }

    const auto status = adminService.approveUser(approvalId, adminUserId, roleCodes);
    if (status == VmsAdminService::MutationStatus::Success) {
        return VmsApiResponse::successResponse({{"result", "APPROVED"}});
    }
    return mutationResponse(status);
});
}

void VmsAdminController::handleRejectUser() {
httpServer.addRoute(HttpMethod::POST, "/admin/users/approvals/{approvalId}/reject", [this](const Http::Request& req) -> Http::Response {
    std::int64_t adminUserId = 0;
    Http::Response auth = requireAdmin(req, adminUserId);
    if (auth.status_code != 0) return auth;

    std::int64_t approvalId = 0;
    if (!VmsRequest::parsePathInt64(req, "approvalId", approvalId)) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto requestBody = VmsRequest::parseJsonObject(req);
    if (!requestBody || !requestBody->contains("reason") || !(*requestBody)["reason"].is_string()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const std::string reason = (*requestBody)["reason"].get<std::string>();
    if (trim(reason).empty()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto status = adminService.rejectUser(approvalId, adminUserId, reason);
    if (status == VmsAdminService::MutationStatus::Success) {
        return VmsApiResponse::successResponse({{"result", "REJECTED"}});
    }
    return mutationResponse(status);
});
}

void VmsAdminController::handleGetUsersRole() {
httpServer.addRoute(HttpMethod::GET, "/admin/users/{userId}/roles", [this](const Http::Request& req) -> Http::Response {
    std::int64_t adminUserId = 0;
    Http::Response auth = requireAdmin(req, adminUserId);
    if (auth.status_code != 0) return auth;

    std::int64_t userId = 0;
    if (!VmsRequest::parsePathInt64(req, "userId", userId)) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto result = adminService.getUserRoles(userId);
    if (result.status == VmsAdminService::ListStatus::NotFound) {
        return VmsApiResponse::notFoundResponse();
    }
    if (result.status != VmsAdminService::ListStatus::Success) {
        return VmsApiResponse::internalServerErrorResponse();
    }

    json roles = json::array();
    for (const auto& role : result.roles) {
        roles.push_back({
            {"roleId", role.roleId},
            {"roleCode", role.roleCode},
            {"roleName", role.roleName}
        });
    }

    return VmsApiResponse::successResponse({
        {"userId", result.userId},
        {"roles", roles}
    });
});
}

void VmsAdminController::handleAddUserRole() {
httpServer.addRoute(HttpMethod::POST, "/admin/users/{userId}/roles", [this](const Http::Request& req) -> Http::Response {
    std::int64_t adminUserId = 0;
    Http::Response auth = requireAdmin(req, adminUserId);
    if (auth.status_code != 0) return auth;

    std::int64_t userId = 0;
    if (!VmsRequest::parsePathInt64(req, "userId", userId)) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto requestBody = VmsRequest::parseJsonObject(req);
    if (!requestBody || !requestBody->contains("roleCode") || !(*requestBody)["roleCode"].is_string()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const std::string roleCode = (*requestBody)["roleCode"].get<std::string>();
    if (trim(roleCode).empty()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    return mutationResponse(adminService.addUserRole(userId, adminUserId, roleCode));
});
}

void VmsAdminController::handleDeleteUserRole() {
httpServer.addRoute(HttpMethod::DELETE, "/admin/users/{userId}/roles/{roleCode}", [this](const Http::Request& req) -> Http::Response {
    std::int64_t adminUserId = 0;
    Http::Response auth = requireAdmin(req, adminUserId);
    if (auth.status_code != 0) return auth;

    std::int64_t userId = 0;
    if (!VmsRequest::parsePathInt64(req, "userId", userId)) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const std::string* roleCode = req.getPathParam("roleCode");
    if (roleCode == nullptr || trim(*roleCode).empty()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    return mutationResponse(adminService.deleteUserRole(userId, *roleCode));
});
}

void VmsAdminController::handleUserDisactivation() {
httpServer.addRoute(HttpMethod::PATCH, "/admin/users/{userId}/status", [this](const Http::Request& req) -> Http::Response {
    std::int64_t adminUserId = 0;
    Http::Response auth = requireAdmin(req, adminUserId);
    if (auth.status_code != 0) return auth;

    std::int64_t userId = 0;
    if (!VmsRequest::parsePathInt64(req, "userId", userId)) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const auto requestBody = VmsRequest::parseJsonObject(req);
    if (!requestBody || !requestBody->contains("isActive") || !(*requestBody)["isActive"].is_boolean()) {
        return VmsApiResponse::invalidArgumentResponse();
    }

    const bool isActive = (*requestBody)["isActive"].get<bool>();
    return mutationResponse(adminService.updateUserActiveStatus(userId, isActive));
});
}
