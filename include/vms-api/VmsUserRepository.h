#ifndef __VMSUSERREPOSITORY_H__
#define __VMSUSERREPOSITORY_H__

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class VmsUserRepository {
public:
    struct UserRecord {
        std::int64_t userId = 0;
        std::string loginId;
        std::string name;
        std::string pwHash;
        bool isActive = true;
        std::string createdAt;
        std::string lastLoginAt;
        std::string approvalStatus;
    };

    VmsUserRepository();
    ~VmsUserRepository();

    std::optional<UserRecord> findByLoginId(const std::string& loginId) const;
    std::optional<UserRecord> findById(std::int64_t userId) const;
    std::vector<std::string> findRoleCodes(std::int64_t userId) const;
    bool createUser(const std::string& loginId, const std::string& name, const std::string& pwHash) const;
    bool createPendingApproval(std::int64_t userId) const;
    bool updateLastLogin(std::int64_t userId) const;
    bool updateName(std::int64_t userId, const std::string& name) const;
    bool updatePassword(std::int64_t userId, const std::string& pwHash) const;
    bool updateNameAndPassword(std::int64_t userId, const std::string& name, const std::string& pwHash) const;
    bool deactivate(std::int64_t userId) const;
};

#endif // __VMSUSERREPOSITORY_H__
