#ifndef __VMSADMINSERVICE_H__
#define __VMSADMINSERVICE_H__

#include <cstdint>
#include <string>
#include <vector>

class VmsAdminRepository;

class VmsAdminService {
public:
    enum class AdminCheckStatus {
        Authorized,
        Forbidden,
        NotFound,
        InternalError
    };

    enum class ListStatus {
        Success,
        NotFound,
        InternalError
    };

    struct ApprovalItem {
        std::int64_t approvalId = 0;
        std::int64_t userId = 0;
        std::string loginId;
        std::string name;
        std::string status;
        std::string requestedAt;
    };

    struct ApprovalListResult {
        ListStatus status = ListStatus::InternalError;
        int page = 1;
        int size = 10;
        std::int64_t totalCount = 0;
        std::vector<ApprovalItem> items;
    };

    enum class MutationStatus {
        Success,
        NotFound,
        InvalidState,
        RoleNotFound,
        AlreadyExists,
        InternalError
    };

    struct RoleItem {
        std::int64_t roleId = 0;
        std::string roleCode;
        std::string roleName;
    };

    struct RoleListResult {
        ListStatus status = ListStatus::InternalError;
        std::int64_t userId = 0;
        std::vector<RoleItem> roles;
    };

public:
    VmsAdminService();
    explicit VmsAdminService(VmsAdminRepository& adminRepository);
    ~VmsAdminService();

    AdminCheckStatus checkAdmin(std::int64_t userId);
    ApprovalListResult getUserApprovals(int page, int size, const std::string& status);
    MutationStatus approveUser(std::int64_t approvalId,
        std::int64_t adminUserId,
        const std::vector<std::string>& roleCodes);
    MutationStatus rejectUser(std::int64_t approvalId,
        std::int64_t adminUserId,
        const std::string& reason);
    RoleListResult getUserRoles(std::int64_t userId);
    MutationStatus addUserRole(std::int64_t userId, std::int64_t adminUserId, const std::string& roleCode);
    MutationStatus deleteUserRole(std::int64_t userId, const std::string& roleCode);
    MutationStatus updateUserActiveStatus(std::int64_t userId, bool isActive);

private:
    VmsAdminRepository& adminRepository;
};

#endif // __VMSADMINSERVICE_H__
