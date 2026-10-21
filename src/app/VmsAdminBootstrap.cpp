#include "VmsAdminBootstrap.h"

#include "DbRowHelpers.h"
#include "DbRuntime.hpp"
#include "LogSync.h"
#include "SecurityUtils.h"

#include <exception>

VmsAdminBootstrap::Status VmsAdminBootstrap::createAdmin(
    const std::string& loginId,
    const std::string& name,
    const std::string& password) const {
    std::string passwordHash;
    try {
        // Keep this identical to the password hashing used by login/signup.
        passwordHash = sha256_hex(password);
    } catch (const std::exception& e) {
        LogSync::error("[create-admin] failed to hash password: ", e.what());
        return Status::InternalError;
    }

    DbTemplate::ParamMap params;
    params.emplace("login_id", loginId);
    params.emplace("name", name);
    params.emplace("pw_hash", passwordHash);

    constexpr std::int64_t kInitialAdminLockId = 7667830472404926464LL;
    const auto rows = AppDb::selectWithAdvisoryLock(
        kInitialAdminLockId, "users.createInitialAdmin", params);
    if (rows.size() != 1) {
        LogSync::error("[create-admin] bootstrap transaction failed");
        return Status::InternalError;
    }

    bool created = false;
    bool adminExists = false;
    bool loginIdExists = false;
    bool roleExists = false;
    const auto& row = rows.front();
    if (!DbRow::getBoolCell(row, "created", created)
        || !DbRow::getBoolCell(row, "admin_exists", adminExists)
        || !DbRow::getBoolCell(row, "login_id_exists", loginIdExists)
        || !DbRow::getBoolCell(row, "role_exists", roleExists)) {
        LogSync::error("[create-admin] invalid bootstrap result");
        return Status::InternalError;
    }

    if (created) return Status::Created;
    if (adminExists) return Status::AdminAlreadyExists;
    if (loginIdExists) return Status::LoginIdAlreadyExists;
    if (!roleExists) return Status::AdminRoleMissing;
    return Status::InternalError;
}
