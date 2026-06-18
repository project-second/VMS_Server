#include "VmsAdminRepository.h"

#include "DbRowHelpers.h"
#include "DbRuntime.hpp"
#include "LogSync.h"

#include <utility>

namespace {

std::optional<VmsAdminRepository::UserRecord> mapUserRecord(const DbTemplate::TypedRow& row) {
    VmsAdminRepository::UserRecord user;
    if (!DbRow::getInt64Cell(row, "user_id", user.userId)) {
        return std::nullopt;
    }

    DbRow::getBoolCell(row, "is_active", user.isActive);
    return user;
}

std::optional<VmsAdminRepository::ApprovalRecord> mapApprovalRecord(const DbTemplate::TypedRow& row) {
    VmsAdminRepository::ApprovalRecord approval;
    if (!DbRow::getInt64Cell(row, "approval_id", approval.approvalId)
        || !DbRow::getInt64Cell(row, "user_id", approval.userId)
        || !DbRow::getStringCell(row, "status", approval.status)) {
        return std::nullopt;
    }

    DbRow::getStringCell(row, "login_id", approval.loginId);
    DbRow::getStringCell(row, "name", approval.name);
    DbRow::getStringCell(row, "requested_at", approval.requestedAt);
    return approval;
}

std::optional<VmsAdminRepository::RoleRecord> mapRoleRecord(const DbTemplate::TypedRow& row) {
    VmsAdminRepository::RoleRecord role;
    if (!DbRow::getInt64Cell(row, "role_id", role.roleId)
        || !DbRow::getStringCell(row, "role_code", role.roleCode)
        || !DbRow::getStringCell(row, "role_name", role.roleName)) {
        return std::nullopt;
    }
    return role;
}

} // namespace

VmsAdminRepository::VmsAdminRepository() = default;

VmsAdminRepository::~VmsAdminRepository() = default;

std::optional<VmsAdminRepository::UserRecord> VmsAdminRepository::findUserById(std::int64_t userId) const {
    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);

    const auto rows = AppDb::select("users.getUserById", params);
    if (rows.empty()) {
        return std::nullopt;
    }

    auto user = mapUserRecord(rows.front());
    if (!user) {
        LogSync::error("[admin.repository] invalid user row for user_id=", userId);
    }
    return user;
}

bool VmsAdminRepository::userHasRole(std::int64_t userId, const std::string& roleCode) const {
    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    params.emplace("role_code", roleCode);
    return !AppDb::select("users.userHasRole", params).empty();
}

std::int64_t VmsAdminRepository::countApprovals(const std::string& status) const {
    DbTemplate::ParamMap params;
    params.emplace("status", status);

    const auto rows = AppDb::select("users.countUserApprovals", params);
    std::int64_t totalCount = -1;
    if (rows.empty() || !DbRow::getInt64Cell(rows.front(), "total_count", totalCount)) {
        LogSync::error("[admin.repository] failed to count approvals");
        return -1;
    }
    return totalCount;
}

std::vector<VmsAdminRepository::ApprovalRecord> VmsAdminRepository::listApprovals(
    const std::string& status,
    int limit,
    int offset) const {
    DbTemplate::ParamMap params;
    params.emplace("status", status);
    params.emplace("limit", static_cast<std::int64_t>(limit));
    params.emplace("offset", static_cast<std::int64_t>(offset));

    std::vector<ApprovalRecord> approvals;
    const auto rows = AppDb::select("users.listUserApprovals", params);
    for (const auto& row : rows) {
        auto approval = mapApprovalRecord(row);
        if (approval) {
            approvals.emplace_back(std::move(*approval));
        } else {
            LogSync::error("[admin.repository] invalid approval row");
        }
    }
    return approvals;
}

std::optional<VmsAdminRepository::ApprovalRecord> VmsAdminRepository::findApprovalById(
    std::int64_t approvalId) const {
    DbTemplate::ParamMap params;
    params.emplace("approval_id", approvalId);

    const auto rows = AppDb::select("users.getApprovalById", params);
    if (rows.empty()) {
        return std::nullopt;
    }

    auto approval = mapApprovalRecord(rows.front());
    if (!approval) {
        LogSync::error("[admin.repository] invalid approval row for approval_id=", approvalId);
    }
    return approval;
}

bool VmsAdminRepository::approveApproval(std::int64_t approvalId, std::int64_t reviewedBy) const {
    DbTemplate::ParamMap params;
    params.emplace("approval_id", approvalId);
    params.emplace("reviewed_by", reviewedBy);
    return AppDb::update("users.approveUserApproval", params);
}

bool VmsAdminRepository::rejectApproval(
    std::int64_t approvalId,
    std::int64_t reviewedBy,
    const std::string& reason) const {
    DbTemplate::ParamMap params;
    params.emplace("approval_id", approvalId);
    params.emplace("reviewed_by", reviewedBy);
    params.emplace("reject_reason", reason);
    return AppDb::update("users.rejectUserApproval", params);
}

std::vector<VmsAdminRepository::RoleRecord> VmsAdminRepository::findUserRoles(std::int64_t userId) const {
    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);

    std::vector<RoleRecord> roles;
    const auto rows = AppDb::select("users.getUserRoles", params);
    for (const auto& row : rows) {
        auto role = mapRoleRecord(row);
        if (role) {
            roles.emplace_back(std::move(*role));
        } else {
            LogSync::error("[admin.repository] invalid role row for user_id=", userId);
        }
    }
    return roles;
}

std::optional<VmsAdminRepository::RoleRecord> VmsAdminRepository::findRoleByCode(
    const std::string& roleCode) const {
    DbTemplate::ParamMap params;
    params.emplace("role_code", roleCode);

    const auto rows = AppDb::select("users.getRoleByCode", params);
    if (rows.empty()) {
        return std::nullopt;
    }

    auto role = mapRoleRecord(rows.front());
    if (!role) {
        LogSync::error("[admin.repository] invalid role row for role_code=", roleCode);
    }
    return role;
}

bool VmsAdminRepository::addUserRole(
    std::int64_t userId,
    std::int64_t roleId,
    std::int64_t assignedBy) const {
    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    params.emplace("role_id", roleId);
    params.emplace("assigned_by", assignedBy);
    return AppDb::insert("users.addUserRole", params);
}

bool VmsAdminRepository::deleteUserRole(std::int64_t userId, std::int64_t roleId) const {
    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    params.emplace("role_id", roleId);
    return AppDb::remove("users.deleteUserRole", params);
}

bool VmsAdminRepository::updateUserActiveStatus(std::int64_t userId, bool isActive) const {
    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    params.emplace("is_active", isActive);
    return AppDb::update("users.updateUserActiveStatus", params);
}
