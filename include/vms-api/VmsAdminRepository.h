#ifndef __VMSADMINREPOSITORY_H__
#define __VMSADMINREPOSITORY_H__

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class VmsAdminRepository {
public:
    struct UserRecord {
        std::int64_t userId = 0;
        bool isActive = true;
    };

    struct ApprovalRecord {
        std::int64_t approvalId = 0;
        std::int64_t userId = 0;
        std::string loginId;
        std::string name;
        std::string status;
        std::string requestedAt;
    };

    struct RoleRecord {
        std::int64_t roleId = 0;
        std::string roleCode;
        std::string roleName;
    };

    VmsAdminRepository();
    ~VmsAdminRepository();

    std::optional<UserRecord> findUserById(std::int64_t userId) const;
    bool userHasRole(std::int64_t userId, const std::string& roleCode) const;

    std::int64_t countApprovals(const std::string& status) const;
    std::vector<ApprovalRecord> listApprovals(const std::string& status, int limit, int offset) const;
    std::optional<ApprovalRecord> findApprovalById(std::int64_t approvalId) const;
    bool approveApproval(std::int64_t approvalId, std::int64_t reviewedBy) const;
    bool rejectApproval(std::int64_t approvalId, std::int64_t reviewedBy, const std::string& reason) const;

    std::vector<RoleRecord> findUserRoles(std::int64_t userId) const;
    std::optional<RoleRecord> findRoleByCode(const std::string& roleCode) const;
    bool addUserRole(std::int64_t userId, std::int64_t roleId, std::int64_t assignedBy) const;
    bool deleteUserRole(std::int64_t userId, std::int64_t roleId) const;

    bool updateUserActiveStatus(std::int64_t userId, bool isActive) const;
};

#endif // __VMSADMINREPOSITORY_H__
