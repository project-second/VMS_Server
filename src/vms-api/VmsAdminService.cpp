#include "VmsAdminService.h"

#include "VmsAdminRepository.h"
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


VmsAdminRepository& defaultAdminRepository() {
    static VmsAdminRepository repository;
    return repository;
}

} // namespace

VmsAdminService::VmsAdminService() : adminRepository(defaultAdminRepository()) {}

VmsAdminService::VmsAdminService(VmsAdminRepository& adminRepository)
    : adminRepository(adminRepository) {}

VmsAdminService::~VmsAdminService() {}

VmsAdminService::AdminCheckStatus VmsAdminService::checkAdmin(std::int64_t userId) {
    const auto user = adminRepository.findUserById(userId);
    if (!user) {
        return AdminCheckStatus::NotFound;
    }

    if (!user->isActive) {
        return AdminCheckStatus::Forbidden;
    }

    if (!adminRepository.userHasRole(userId, "ADMIN")) {
        return AdminCheckStatus::Forbidden;
    }

    return AdminCheckStatus::Authorized;
}

VmsAdminService::ApprovalListResult VmsAdminService::getUserApprovals(
    int page, int size, const std::string& status) {
    const int normalizedPage = std::max(1, page);
    const int normalizedSize = std::max(1, std::min(size, 100));
    const std::string normalizedStatus = toUpperAscii(trim(status));

    ApprovalListResult result;
    result.page = normalizedPage;
    result.size = normalizedSize;

    result.totalCount = adminRepository.countApprovals(normalizedStatus);
    if (result.totalCount < 0) {
        LogSync::error("[admin.approvals] failed to count approvals");
        return result;
    }

    const auto approvals = adminRepository.listApprovals(
        normalizedStatus,
        normalizedSize,
        (normalizedPage - 1) * normalizedSize);
    for (const auto& approval : approvals) {
        ApprovalItem item;
        item.approvalId = approval.approvalId;
        item.userId = approval.userId;
        item.loginId = approval.loginId;
        item.name = approval.name;
        item.status = approval.status;
        item.requestedAt = approval.requestedAt;
        result.items.emplace_back(std::move(item));
    }

    result.status = ListStatus::Success;
    return result;
}

VmsAdminService::MutationStatus VmsAdminService::approveUser(
    std::int64_t approvalId,
    std::int64_t adminUserId,
    const std::vector<std::string>& roleCodes) {
    const auto approval = adminRepository.findApprovalById(approvalId);
    if (!approval) {
        return MutationStatus::NotFound;
    }

    if (toUpperAscii(trim(approval->status)) != "PENDING") {
        return MutationStatus::InvalidState;
    }

    for (const std::string& roleCode : roleCodes) {
        if (!adminRepository.findRoleByCode(toUpperAscii(trim(roleCode)))) {
            return MutationStatus::RoleNotFound;
        }
    }

    if (!adminRepository.approveApproval(approvalId, adminUserId)) {
        LogSync::error("[admin.approve] update approval failed for approval_id=", approvalId);
        return MutationStatus::InternalError;
    }

    if (!adminRepository.updateUserActiveStatus(approval->userId, true)) {
        LogSync::error("[admin.approve] activate user failed for user_id=", approval->userId);
        return MutationStatus::InternalError;
    }

    for (const std::string& roleCode : roleCodes) {
        const MutationStatus roleStatus = addUserRole(approval->userId, adminUserId, roleCode);
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
    const auto approval = adminRepository.findApprovalById(approvalId);
    if (!approval) {
        return MutationStatus::NotFound;
    }

    if (toUpperAscii(trim(approval->status)) != "PENDING") {
        return MutationStatus::InvalidState;
    }

    if (!adminRepository.rejectApproval(approvalId, adminUserId, reason)) {
        LogSync::error("[admin.reject] update approval failed for approval_id=", approvalId);
        return MutationStatus::InternalError;
    }

    return MutationStatus::Success;
}

VmsAdminService::RoleListResult VmsAdminService::getUserRoles(std::int64_t userId) {
    RoleListResult result;
    result.userId = userId;

    if (!adminRepository.findUserById(userId)) {
        result.status = ListStatus::NotFound;
        return result;
    }

    const auto roles = adminRepository.findUserRoles(userId);
    for (const auto& role : roles) {
        RoleItem item;
        item.roleId = role.roleId;
        item.roleCode = role.roleCode;
        item.roleName = role.roleName;
        result.roles.emplace_back(std::move(item));
    }

    result.status = ListStatus::Success;
    return result;
}

VmsAdminService::MutationStatus VmsAdminService::addUserRole(
    std::int64_t userId, std::int64_t adminUserId, const std::string& roleCode) {
    if (!adminRepository.findUserById(userId)) {
        return MutationStatus::NotFound;
    }

    const std::string normalizedRoleCode = toUpperAscii(trim(roleCode));
    const auto role = adminRepository.findRoleByCode(normalizedRoleCode);
    if (!role) {
        return MutationStatus::RoleNotFound;
    }

    if (adminRepository.userHasRole(userId, normalizedRoleCode)) {
        return MutationStatus::AlreadyExists;
    }

    if (!adminRepository.addUserRole(userId, role->roleId, adminUserId)) {
        LogSync::error("[admin.roles.add] insert failed for user_id=", userId, ", role_code=", normalizedRoleCode);
        return MutationStatus::InternalError;
    }

    return MutationStatus::Success;
}

VmsAdminService::MutationStatus VmsAdminService::deleteUserRole(std::int64_t userId, const std::string& roleCode) {
    if (!adminRepository.findUserById(userId)) {
        return MutationStatus::NotFound;
    }

    const std::string normalizedRoleCode = toUpperAscii(trim(roleCode));
    const auto role = adminRepository.findRoleByCode(normalizedRoleCode);
    if (!role) {
        return MutationStatus::RoleNotFound;
    }

    if (!adminRepository.userHasRole(userId, normalizedRoleCode)) {
        return MutationStatus::NotFound;
    }

    if (!adminRepository.deleteUserRole(userId, role->roleId)) {
        LogSync::error("[admin.roles.delete] delete failed for user_id=", userId, ", role_code=", normalizedRoleCode);
        return MutationStatus::InternalError;
    }

    return MutationStatus::Success;
}

VmsAdminService::MutationStatus VmsAdminService::updateUserActiveStatus(std::int64_t userId, bool isActive) {
    if (!adminRepository.findUserById(userId)) {
        return MutationStatus::NotFound;
    }

    if (!adminRepository.updateUserActiveStatus(userId, isActive)) {
        LogSync::error("[admin.status] update failed for user_id=", userId);
        return MutationStatus::InternalError;
    }

    return MutationStatus::Success;
}
