#include "VmsAdminService.h"

#include "DbRuntime.hpp"
#include "DbRowHelpers.h"
#include "LogSync.h"
#include "StringHelpers.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace {

std::string toUpperAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

DbTemplate::TypedRowSet selectUserById(std::int64_t userId) {
    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    return AppDb::select("users.getUserById", params);
}

DbTemplate::TypedRowSet selectApprovalById(std::int64_t approvalId) {
    DbTemplate::ParamMap params;
    params.emplace("approval_id", approvalId);
    return AppDb::select("users.getApprovalById", params);
}

bool roleExists(const std::string& roleCode, std::int64_t& roleId) {
    DbTemplate::ParamMap params;
    params.emplace("role_code", roleCode);
    const auto roles = AppDb::select("users.getRoleByCode", params);
    if (roles.empty()) return false;
    return DbRow::getInt64Cell(roles.front(), "role_id", roleId);
}

} // namespace

VmsAdminService::VmsAdminService() {}

VmsAdminService::~VmsAdminService() {}

VmsAdminService::AdminCheckStatus VmsAdminService::checkAdmin(std::int64_t userId) {
    const auto users = selectUserById(userId);
    if (users.empty()) {
        return AdminCheckStatus::NotFound;
    }

    bool isActive = true;
    if (DbRow::getBoolCell(users.front(), "is_active", isActive) && !isActive) {
        return AdminCheckStatus::Forbidden;
    }

    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    params.emplace("role_code", std::string("ADMIN"));
    const auto rows = AppDb::select("users.userHasRole", params);
    if (rows.empty()) {
        return AdminCheckStatus::Forbidden;
    }

    return AdminCheckStatus::Authorized;
}

VmsAdminService::ApprovalListResult VmsAdminService::getUserApprovals(
    int page, int size, const std::string& status) {
    const int normalizedPage = std::max(1, page);
    const int normalizedSize = std::max(1, std::min(size, 100));
    const std::string normalizedStatus = toUpperAscii(trim(status));

    DbTemplate::ParamMap params;
    params.emplace("status", normalizedStatus);
    params.emplace("limit", static_cast<std::int64_t>(normalizedSize));
    params.emplace("offset", static_cast<std::int64_t>((normalizedPage - 1) * normalizedSize));

    ApprovalListResult result;
    result.page = normalizedPage;
    result.size = normalizedSize;

    const auto countRows = AppDb::select("users.countUserApprovals", params);
    if (countRows.empty() || !DbRow::getInt64Cell(countRows.front(), "total_count", result.totalCount)) {
        LogSync::error("[admin.approvals] failed to count approvals");
        return result;
    }

    const auto rows = AppDb::select("users.listUserApprovals", params);
    for (const auto& row : rows) {
        ApprovalItem item;
        if (!DbRow::getInt64Cell(row, "approval_id", item.approvalId)
            || !DbRow::getInt64Cell(row, "user_id", item.userId)
            || !DbRow::getStringCell(row, "login_id", item.loginId)
            || !DbRow::getStringCell(row, "name", item.name)
            || !DbRow::getStringCell(row, "status", item.status)) {
            LogSync::error("[admin.approvals] invalid row format");
            return ApprovalListResult{};
        }
        DbRow::getStringCell(row, "requested_at", item.requestedAt);
        result.items.emplace_back(std::move(item));
    }

    result.status = ListStatus::Success;
    return result;
}

VmsAdminService::MutationStatus VmsAdminService::approveUser(
    std::int64_t approvalId,
    std::int64_t adminUserId,
    const std::vector<std::string>& roleCodes) {
    const auto approvals = selectApprovalById(approvalId);
    if (approvals.empty()) {
        return MutationStatus::NotFound;
    }

    std::int64_t userId = 0;
    std::string status;
    if (!DbRow::getInt64Cell(approvals.front(), "user_id", userId)
        || !DbRow::getStringCell(approvals.front(), "status", status)) {
        LogSync::error("[admin.approve] invalid approval row for approval_id=", approvalId);
        return MutationStatus::InternalError;
    }
    if (toUpperAscii(trim(status)) != "PENDING") {
        return MutationStatus::InvalidState;
    }

    for (const std::string& roleCode : roleCodes) {
        std::int64_t roleId = 0;
        if (!roleExists(toUpperAscii(trim(roleCode)), roleId)) {
            return MutationStatus::RoleNotFound;
        }
    }

    DbTemplate::ParamMap approvalParams;
    approvalParams.emplace("approval_id", approvalId);
    approvalParams.emplace("reviewed_by", adminUserId);
    if (!AppDb::update("users.approveUserApproval", approvalParams)) {
        LogSync::error("[admin.approve] update approval failed for approval_id=", approvalId);
        return MutationStatus::InternalError;
    }

    DbTemplate::ParamMap activeParams;
    activeParams.emplace("user_id", userId);
    activeParams.emplace("is_active", true);
    if (!AppDb::update("users.updateUserActiveStatus", activeParams)) {
        LogSync::error("[admin.approve] activate user failed for user_id=", userId);
        return MutationStatus::InternalError;
    }

    for (const std::string& roleCode : roleCodes) {
        const MutationStatus roleStatus = addUserRole(userId, adminUserId, roleCode);
        if (roleStatus != MutationStatus::Success && roleStatus != MutationStatus::AlreadyExists) {
            return roleStatus;
        }
    }

    return MutationStatus::Success;
}

VmsAdminService::MutationStatus VmsAdminService::rejectUser(
    std::int64_t approvalId,
    std::int64_t adminUserId,
    const std::string& reason) {
    const auto approvals = selectApprovalById(approvalId);
    if (approvals.empty()) {
        return MutationStatus::NotFound;
    }

    std::string status;
    if (!DbRow::getStringCell(approvals.front(), "status", status)) {
        LogSync::error("[admin.reject] invalid approval row for approval_id=", approvalId);
        return MutationStatus::InternalError;
    }
    if (toUpperAscii(trim(status)) != "PENDING") {
        return MutationStatus::InvalidState;
    }

    DbTemplate::ParamMap params;
    params.emplace("approval_id", approvalId);
    params.emplace("reviewed_by", adminUserId);
    params.emplace("reject_reason", reason);
    if (!AppDb::update("users.rejectUserApproval", params)) {
        LogSync::error("[admin.reject] update approval failed for approval_id=", approvalId);
        return MutationStatus::InternalError;
    }

    return MutationStatus::Success;
}

VmsAdminService::RoleListResult VmsAdminService::getUserRoles(std::int64_t userId) {
    RoleListResult result;
    result.userId = userId;

    if (selectUserById(userId).empty()) {
        result.status = ListStatus::NotFound;
        return result;
    }

    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    const auto rows = AppDb::select("users.getUserRoles", params);
    for (const auto& row : rows) {
        RoleItem item;
        if (!DbRow::getInt64Cell(row, "role_id", item.roleId)
            || !DbRow::getStringCell(row, "role_code", item.roleCode)
            || !DbRow::getStringCell(row, "role_name", item.roleName)) {
            LogSync::error("[admin.roles] invalid role row for user_id=", userId);
            return RoleListResult{};
        }
        result.roles.emplace_back(std::move(item));
    }

    result.status = ListStatus::Success;
    return result;
}

VmsAdminService::MutationStatus VmsAdminService::addUserRole(
    std::int64_t userId, std::int64_t adminUserId, const std::string& roleCode) {
    if (selectUserById(userId).empty()) {
        return MutationStatus::NotFound;
    }

    std::int64_t roleId = 0;
    const std::string normalizedRoleCode = toUpperAscii(trim(roleCode));
    if (!roleExists(normalizedRoleCode, roleId)) {
        return MutationStatus::RoleNotFound;
    }

    DbTemplate::ParamMap existsParams;
    existsParams.emplace("user_id", userId);
    existsParams.emplace("role_code", normalizedRoleCode);
    if (!AppDb::select("users.userHasRole", existsParams).empty()) {
        return MutationStatus::AlreadyExists;
    }

    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    params.emplace("role_id", roleId);
    params.emplace("assigned_by", adminUserId);
    if (!AppDb::insert("users.addUserRole", params)) {
        LogSync::error("[admin.roles.add] insert failed for user_id=", userId, ", role_code=", normalizedRoleCode);
        return MutationStatus::InternalError;
    }

    return MutationStatus::Success;
}

VmsAdminService::MutationStatus VmsAdminService::deleteUserRole(std::int64_t userId, const std::string& roleCode) {
    if (selectUserById(userId).empty()) {
        return MutationStatus::NotFound;
    }

    std::int64_t roleId = 0;
    const std::string normalizedRoleCode = toUpperAscii(trim(roleCode));
    if (!roleExists(normalizedRoleCode, roleId)) {
        return MutationStatus::RoleNotFound;
    }

    DbTemplate::ParamMap existsParams;
    existsParams.emplace("user_id", userId);
    existsParams.emplace("role_code", normalizedRoleCode);
    if (AppDb::select("users.userHasRole", existsParams).empty()) {
        return MutationStatus::NotFound;
    }

    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    params.emplace("role_id", roleId);
    if (!AppDb::remove("users.deleteUserRole", params)) {
        LogSync::error("[admin.roles.delete] delete failed for user_id=", userId, ", role_code=", normalizedRoleCode);
        return MutationStatus::InternalError;
    }

    return MutationStatus::Success;
}

VmsAdminService::MutationStatus VmsAdminService::updateUserActiveStatus(std::int64_t userId, bool isActive) {
    if (selectUserById(userId).empty()) {
        return MutationStatus::NotFound;
    }

    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    params.emplace("is_active", isActive);
    if (!AppDb::update("users.updateUserActiveStatus", params)) {
        LogSync::error("[admin.status] update failed for user_id=", userId);
        return MutationStatus::InternalError;
    }

    return MutationStatus::Success;
}
