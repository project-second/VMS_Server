#include "VmsUserRepository.h"

#include "DbRowHelpers.h"
#include "DbRuntime.hpp"
#include "LogSync.h"

#include <utility>

namespace {

std::optional<VmsUserRepository::UserRecord> mapUserRecord(
    const DbTemplate::TypedRow& row,
    bool requirePasswordHash) {
    VmsUserRepository::UserRecord user;
    if (!DbRow::getInt64Cell(row, "user_id", user.userId)
        || !DbRow::getStringCell(row, "login_id", user.loginId)
        || !DbRow::getStringCell(row, "name", user.name)) {
        return std::nullopt;
    }

    if (requirePasswordHash && !DbRow::getStringCell(row, "pw_hash", user.pwHash)) {
        return std::nullopt;
    }

    DbRow::getStringCell(row, "pw_hash", user.pwHash);
    DbRow::getBoolCell(row, "is_active", user.isActive);
    DbRow::getStringCell(row, "created_at", user.createdAt);
    DbRow::getStringCell(row, "last_login_at", user.lastLoginAt);
    DbRow::getStringCell(row, "approval_status", user.approvalStatus);
    return user;
}


} // namespace

VmsUserRepository::VmsUserRepository() = default;

VmsUserRepository::~VmsUserRepository() = default;

std::optional<VmsUserRepository::UserRecord> VmsUserRepository::findByLoginId(const std::string& loginId) const {
    DbTemplate::ParamMap params;
    params.emplace("login_id", loginId);

    const auto rows = AppDb::select("users.getUserByLoginId", params);
    if (rows.empty()) {
        return std::nullopt;
    }

    auto user = mapUserRecord(rows.front(), true);
    if (!user) {
        LogSync::error("[users.repository] invalid row format for login_id=", loginId);
    }
    return user;
}

std::optional<VmsUserRepository::UserRecord> VmsUserRepository::findById(std::int64_t userId) const {
    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);

    const auto rows = AppDb::select("users.getUserById", params);
    if (rows.empty()) {
        return std::nullopt;
    }

    auto user = mapUserRecord(rows.front(), true);
    if (!user) {
        LogSync::error("[users.repository] invalid row format for user_id=", userId);
    }
    return user;
}

std::vector<std::string> VmsUserRepository::findRoleCodes(std::int64_t userId) const {
    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);

    std::vector<std::string> roleCodes;
    const auto rows = AppDb::select("users.getUserRoles", params);
    for (const auto& row : rows) {
        std::string roleCode;
        if (DbRow::getStringCell(row, "role_code", roleCode)) {
            roleCodes.emplace_back(std::move(roleCode));
        }
    }
    return roleCodes;
}

bool VmsUserRepository::createUser(
    const std::string& loginId,
    const std::string& name,
    const std::string& pwHash) const {
    DbTemplate::ParamMap params;
    params.emplace("login_id", loginId);
    params.emplace("name", name);
    params.emplace("pw_hash", pwHash);
    return AppDb::insert("users.createUser", params);
}

bool VmsUserRepository::createPendingApproval(std::int64_t userId) const {
    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    return AppDb::insert("users.createPendingApproval", params);
}

bool VmsUserRepository::updateLastLogin(std::int64_t userId) const {
    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    return AppDb::update("users.updateLastLogin", params);
}

bool VmsUserRepository::updateName(std::int64_t userId, const std::string& name) const {
    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    params.emplace("name", name);
    return AppDb::update("users.updateUserName", params);
}

bool VmsUserRepository::updatePassword(std::int64_t userId, const std::string& pwHash) const {
    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    params.emplace("pw_hash", pwHash);
    return AppDb::update("users.updateUserPassword", params);
}

bool VmsUserRepository::updateNameAndPassword(
    std::int64_t userId,
    const std::string& name,
    const std::string& pwHash) const {
    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    params.emplace("name", name);
    params.emplace("pw_hash", pwHash);
    return AppDb::update("users.updateUserNameAndPassword", params);
}

bool VmsUserRepository::deactivate(std::int64_t userId) const {
    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    return AppDb::update("users.deactivateUser", params);
}
