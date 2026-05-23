#include "VmsUserService.h"

#include "DbRuntime.hpp"
#include "DbRowHelpers.h"
#include "LogSync.h"
#include "SecurityUtils.h"
#include "StringHelpers.h"

#include <chrono>
#include <exception>
#include <mutex>
#include <unordered_map>

namespace {

constexpr std::int64_t kAccessTokenExpiresInSeconds = 3600;
constexpr std::int64_t kRefreshTokenExpiresInSeconds = 604800;

struct TokenSession {
    std::int64_t userId = 0;
    std::chrono::system_clock::time_point expiresAt;
};

std::unordered_map<std::string, TokenSession> g_accessTokenSessions;
std::unordered_map<std::string, TokenSession> g_refreshTokenSessions;
std::mutex g_tokenSessionsMutex;

void pruneExpiredTokensLocked(const std::chrono::system_clock::time_point now) {
    for (auto it = g_accessTokenSessions.begin(); it != g_accessTokenSessions.end();) {
        if (it->second.expiresAt <= now) {
            it = g_accessTokenSessions.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = g_refreshTokenSessions.begin(); it != g_refreshTokenSessions.end();) {
        if (it->second.expiresAt <= now) {
            it = g_refreshTokenSessions.erase(it);
        } else {
            ++it;
        }
    }
}

void removeUserTokensLocked(std::int64_t userId) {
    for (auto it = g_accessTokenSessions.begin(); it != g_accessTokenSessions.end();) {
        if (it->second.userId == userId) {
            it = g_accessTokenSessions.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = g_refreshTokenSessions.begin(); it != g_refreshTokenSessions.end();) {
        if (it->second.userId == userId) {
            it = g_refreshTokenSessions.erase(it);
        } else {
            ++it;
        }
    }
}

std::string buildToken(const std::string& prefix, std::int64_t userId) {
    return sha256_hex(prefix + ":" + std::to_string(userId) + ":" + random_hex(64));
}

bool verifyPassword(const DbTemplate::TypedRow& user, const std::string& password) {
    std::string pwHash;
    if (!DbRow::getStringCell(user, "pw_hash", pwHash)) return false;
    return sha256_hex(password) == pwHash;
}

DbTemplate::TypedRowSet selectUserById(std::int64_t userId) {
    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    return AppDb::select("users.getUserById", params);
}

} // namespace

VmsUserService::VmsUserService() {}

VmsUserService::~VmsUserService() {}

VmsUserService::LoginResult VmsUserService::login(const std::string& loginId, const std::string& pw) {
    DbTemplate::ParamMap params;
    params.emplace("login_id", loginId);
    const auto users = AppDb::select("users.getUserByLoginId", params);
    if (users.empty()) {
        LoginResult result;
        result.status = LoginStatus::InvalidCredentials;
        return result;
    }

    const DbTemplate::TypedRow& user = users.front();

    std::int64_t userId = 0;
    if (!DbRow::getInt64Cell(user, "user_id", userId)) {
        LogSync::error("[users.login] invalid row format: missing user_id for login_id=", loginId);
        return LoginResult{};
    }

    try {
        if (!verifyPassword(user, pw)) {
            LoginResult result;
            result.status = LoginStatus::InvalidCredentials;
            return result;
        }
    } catch (const std::exception& e) {
        LogSync::error("[users.login] failed to verify password: ", e.what());
        return LoginResult{};
    }

    std::string approvalStatus;
    if (DbRow::getStringCell(user, "approval_status", approvalStatus)) {
        approvalStatus = to_lower(trim(approvalStatus));
        if (approvalStatus == "pending") {
            LoginResult result;
            result.status = LoginStatus::PendingApproval;
            return result;
        }
        if (approvalStatus == "rejected") {
            LoginResult result;
            result.status = LoginStatus::Rejected;
            return result;
        }
    }

    bool isActive = true;
    if (DbRow::getBoolCell(user, "is_active", isActive) && !isActive) {
        LoginResult result;
        result.status = LoginStatus::Inactive;
        return result;
    }

    DbTemplate::ParamMap updateParams;
    updateParams.emplace("user_id", userId);
    if (!AppDb::update("users.updateLastLogin", updateParams)) {
        LogSync::error("[users.login] users.updateLastLogin failed for user_id=", userId);
    }

    try {
        const auto now = std::chrono::system_clock::now();
        LoginResult result;
        result.status = LoginStatus::Success;
        result.accessToken = buildToken("access", userId);
        result.refreshToken = buildToken("refresh", userId);
        result.expiresIn = kAccessTokenExpiresInSeconds;
        result.refreshExpiresIn = kRefreshTokenExpiresInSeconds;
        result.issuedAt = utc_now_iso8601();

        std::lock_guard<std::mutex> lock(g_tokenSessionsMutex);
        pruneExpiredTokensLocked(now);
        g_accessTokenSessions[result.accessToken] = TokenSession{userId, now + std::chrono::seconds(kAccessTokenExpiresInSeconds)};
        g_refreshTokenSessions[result.refreshToken] = TokenSession{userId, now + std::chrono::seconds(kRefreshTokenExpiresInSeconds)};
        return result;
    } catch (const std::exception& e) {
        LogSync::error("[users.login] failed to create tokens: ", e.what());
        return LoginResult{};
    }
}

VmsUserService::SignupStatus VmsUserService::signup(
    const std::string& loginId, const std::string& name, const std::string& pw) {
    DbTemplate::ParamMap selectParams;
    selectParams.emplace("login_id", loginId);
    const auto users = AppDb::select("users.getUserByLoginId", selectParams);
    if (!users.empty()) {
        return SignupStatus::LoginIdAlreadyExists;
    }

    DbTemplate::ParamMap insertParams;
    insertParams.emplace("login_id", loginId);
    insertParams.emplace("name", name);
    try {
        insertParams.emplace("pw_hash", sha256_hex(pw));
    } catch (const std::exception& e) {
        LogSync::error("[users.signup] failed to hash password: ", e.what());
        return SignupStatus::InternalError;
    }

    if (!AppDb::insert("users.createUser", insertParams)) {
        LogSync::error("[users.signup] users.createUser failed for login_id=", loginId);
        return SignupStatus::InternalError;
    }

    const auto createdUsers = AppDb::select("users.getUserByLoginId", selectParams);
    if (createdUsers.empty()) {
        LogSync::error("[users.signup] created user not found for login_id=", loginId);
        return SignupStatus::InternalError;
    }

    std::int64_t userId = 0;
    if (!DbRow::getInt64Cell(createdUsers.front(), "user_id", userId)) {
        LogSync::error("[users.signup] invalid created row format for login_id=", loginId);
        return SignupStatus::InternalError;
    }

    DbTemplate::ParamMap approvalParams;
    approvalParams.emplace("user_id", userId);
    if (!AppDb::insert("users.createPendingApproval", approvalParams)) {
        LogSync::error("[users.signup] users.createPendingApproval failed for user_id=", userId);
        return SignupStatus::InternalError;
    }

    return SignupStatus::Created;
}

VmsUserService::LogoutStatus VmsUserService::logout(const std::string& accessToken, const std::string& refreshToken) {
    const std::string normalizedAccess = trim(accessToken);
    const std::string normalizedRefresh = trim(refreshToken);
    if (normalizedAccess.empty() || normalizedRefresh.empty()) {
        return LogoutStatus::InvalidToken;
    }

    const auto now = std::chrono::system_clock::now();
    std::lock_guard<std::mutex> lock(g_tokenSessionsMutex);
    pruneExpiredTokensLocked(now);

    const auto accessIt = g_accessTokenSessions.find(normalizedAccess);
    const auto refreshIt = g_refreshTokenSessions.find(normalizedRefresh);
    if (accessIt == g_accessTokenSessions.end() || refreshIt == g_refreshTokenSessions.end()) {
        return LogoutStatus::InvalidToken;
    }
    if (accessIt->second.userId != refreshIt->second.userId) {
        return LogoutStatus::InvalidToken;
    }

    g_accessTokenSessions.erase(accessIt);
    g_refreshTokenSessions.erase(refreshIt);
    return LogoutStatus::Success;
}

VmsUserService::RefreshResult VmsUserService::refreshToken(const std::string& refreshToken) {
    const std::string normalizedRefresh = trim(refreshToken);
    if (normalizedRefresh.empty()) {
        RefreshResult result;
        result.status = RefreshStatus::InvalidRefreshToken;
        return result;
    }

    const auto now = std::chrono::system_clock::now();
    std::int64_t userId = 0;
    {
        std::lock_guard<std::mutex> lock(g_tokenSessionsMutex);
        pruneExpiredTokensLocked(now);

        const auto it = g_refreshTokenSessions.find(normalizedRefresh);
        if (it == g_refreshTokenSessions.end()) {
            RefreshResult result;
            result.status = RefreshStatus::InvalidRefreshToken;
            return result;
        }

        userId = it->second.userId;
        g_refreshTokenSessions.erase(it);
    }

    const auto users = selectUserById(userId);
    if (users.empty()) {
        RefreshResult result;
        result.status = RefreshStatus::InvalidRefreshToken;
        return result;
    }

    bool isActive = true;
    if (DbRow::getBoolCell(users.front(), "is_active", isActive) && !isActive) {
        RefreshResult result;
        result.status = RefreshStatus::InvalidRefreshToken;
        return result;
    }

    try {
        RefreshResult result;
        result.status = RefreshStatus::Success;
        result.accessToken = buildToken("access", userId);
        result.refreshToken = buildToken("refresh", userId);
        result.expiresIn = kAccessTokenExpiresInSeconds;
        result.refreshExpiresIn = kRefreshTokenExpiresInSeconds;
        result.issuedAt = utc_now_iso8601();

        std::lock_guard<std::mutex> lock(g_tokenSessionsMutex);
        pruneExpiredTokensLocked(now);
        g_accessTokenSessions[result.accessToken] = TokenSession{userId, now + std::chrono::seconds(kAccessTokenExpiresInSeconds)};
        g_refreshTokenSessions[result.refreshToken] = TokenSession{userId, now + std::chrono::seconds(kRefreshTokenExpiresInSeconds)};
        return result;
    } catch (const std::exception& e) {
        LogSync::error("[users.refresh] failed to create tokens: ", e.what());
        return RefreshResult{};
    }
}

VmsUserService::TokenValidationResult VmsUserService::validateAccessToken(const std::string& accessToken) const {
    const std::string normalized = trim(accessToken);
    if (normalized.size() != 64 || !is_hex_string(normalized)) {
        return TokenValidationResult{};
    }

    const auto now = std::chrono::system_clock::now();
    std::lock_guard<std::mutex> lock(g_tokenSessionsMutex);
    pruneExpiredTokensLocked(now);

    const auto it = g_accessTokenSessions.find(normalized);
    if (it == g_accessTokenSessions.end()) {
        return TokenValidationResult{};
    }

    TokenValidationResult result;
    result.status = TokenStatus::Valid;
    result.userId = it->second.userId;
    return result;
}

VmsUserService::TokenValidationResult VmsUserService::validateRefreshToken(const std::string& refreshToken) const {
    const std::string normalized = trim(refreshToken);
    if (normalized.size() != 64 || !is_hex_string(normalized)) {
        return TokenValidationResult{};
    }

    const auto now = std::chrono::system_clock::now();
    std::lock_guard<std::mutex> lock(g_tokenSessionsMutex);
    pruneExpiredTokensLocked(now);

    const auto it = g_refreshTokenSessions.find(normalized);
    if (it == g_refreshTokenSessions.end()) {
        return TokenValidationResult{};
    }

    TokenValidationResult result;
    result.status = TokenStatus::Valid;
    result.userId = it->second.userId;
    return result;
}

VmsUserService::UserInfoResult VmsUserService::getUserInfo(std::int64_t userId) {
    const auto users = selectUserById(userId);
    if (users.empty()) {
        UserInfoResult result;
        result.status = UserInfoStatus::NotFound;
        return result;
    }

    const DbTemplate::TypedRow& user = users.front();
    UserInfoResult result;
    result.status = UserInfoStatus::Success;
    if (!DbRow::getInt64Cell(user, "user_id", result.userId)
        || !DbRow::getStringCell(user, "login_id", result.loginId)
        || !DbRow::getStringCell(user, "name", result.name)) {
        LogSync::error("[users.me] invalid row format for user_id=", userId);
        return UserInfoResult{};
    }

    DbRow::getStringCell(user, "created_at", result.createdAt);
    DbRow::getStringCell(user, "last_login_at", result.lastLoginAt);

    DbTemplate::ParamMap roleParams;
    roleParams.emplace("user_id", userId);
    const auto roles = AppDb::select("users.getUserRoles", roleParams);
    for (const auto& role : roles) {
        std::string roleCode;
        if (DbRow::getStringCell(role, "role_code", roleCode)) {
            result.roles.emplace_back(std::move(roleCode));
        }
    }

    return result;
}

VmsUserService::UpdateResult VmsUserService::updateUserInfo(
    std::int64_t userId,
    const std::string& currentPassword,
    const std::string& name,
    const std::string& newPassword) {
    const auto users = selectUserById(userId);
    if (users.empty()) {
        UpdateResult result;
        result.status = UpdateStatus::NotFound;
        return result;
    }

    try {
        if (!verifyPassword(users.front(), currentPassword)) {
            UpdateResult result;
            result.status = UpdateStatus::InvalidPassword;
            return result;
        }
    } catch (const std::exception& e) {
        LogSync::error("[users.update] failed to verify password: ", e.what());
        return UpdateResult{};
    }

    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    params.emplace("name", name);
    try {
        params.emplace("pw_hash", newPassword.empty() ? std::string{} : sha256_hex(newPassword));
    } catch (const std::exception& e) {
        LogSync::error("[users.update] failed to hash new password: ", e.what());
        return UpdateResult{};
    }

    const bool updateName = !name.empty();
    const bool updatePassword = !newPassword.empty();
    bool ok = true;
    if (updateName && updatePassword) {
        ok = AppDb::update("users.updateUserNameAndPassword", params);
    } else if (updateName) {
        ok = AppDb::update("users.updateUserName", params);
    } else if (updatePassword) {
        ok = AppDb::update("users.updateUserPassword", params);
    }

    if (!ok) {
        LogSync::error("[users.update] update failed for user_id=", userId);
        return UpdateResult{};
    }

    const auto updatedUsers = selectUserById(userId);
    if (updatedUsers.empty()) {
        UpdateResult result;
        result.status = UpdateStatus::NotFound;
        return result;
    }

    UpdateResult result;
    result.status = UpdateStatus::Success;
    DbRow::getInt64Cell(updatedUsers.front(), "user_id", result.userId);
    DbRow::getStringCell(updatedUsers.front(), "login_id", result.loginId);
    DbRow::getStringCell(updatedUsers.front(), "name", result.name);
    result.updatedAt = utc_now_iso8601();
    return result;
}

VmsUserService::DeleteStatus VmsUserService::deleteUser(std::int64_t userId, const std::string& currentPassword) {
    const auto users = selectUserById(userId);
    if (users.empty()) {
        return DeleteStatus::NotFound;
    }

    try {
        if (!verifyPassword(users.front(), currentPassword)) {
            return DeleteStatus::InvalidPassword;
        }
    } catch (const std::exception& e) {
        LogSync::error("[users.delete] failed to verify password: ", e.what());
        return DeleteStatus::InternalError;
    }

    DbTemplate::ParamMap params;
    params.emplace("user_id", userId);
    if (!AppDb::update("users.deactivateUser", params)) {
        LogSync::error("[users.delete] deactivate failed for user_id=", userId);
        return DeleteStatus::InternalError;
    }

    std::lock_guard<std::mutex> lock(g_tokenSessionsMutex);
    removeUserTokensLocked(userId);
    return DeleteStatus::Success;
}
