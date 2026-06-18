#include "VmsUserService.h"

#include "VmsUserRepository.h"
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

bool verifyPassword(const VmsUserRepository::UserRecord& user, const std::string& password) {
    return sha256_hex(password) == user.pwHash;
}

VmsUserRepository& defaultUserRepository() {
    static VmsUserRepository repository;
    return repository;
}

} // namespace

VmsUserService::VmsUserService() : userRepository(defaultUserRepository()) {}

VmsUserService::VmsUserService(VmsUserRepository& userRepository)
    : userRepository(userRepository) {}

VmsUserService::~VmsUserService() {}

VmsUserService::LoginResult VmsUserService::login(const std::string& loginId, const std::string& pw) {
    const auto user = userRepository.findByLoginId(loginId);
    if (!user) {
        LoginResult result;
        result.status = LoginStatus::InvalidCredentials;
        return result;
    }

    try {
        if (!verifyPassword(*user, pw)) {
            LoginResult result;
            result.status = LoginStatus::InvalidCredentials;
            return result;
        }
    } catch (const std::exception& e) {
        LogSync::error("[users.login] failed to verify password: ", e.what());
        return LoginResult{};
    }

    const std::string approvalStatus = to_lower(trim(user->approvalStatus));
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

    if (!user->isActive) {
        LoginResult result;
        result.status = LoginStatus::Inactive;
        return result;
    }

    if (!userRepository.updateLastLogin(user->userId)) {
        LogSync::error("[users.login] users.updateLastLogin failed for user_id=", user->userId);
    }

    try {
        const auto now = std::chrono::system_clock::now();
        LoginResult result;
        result.status = LoginStatus::Success;
        result.accessToken = buildToken("access", user->userId);
        result.refreshToken = buildToken("refresh", user->userId);
        result.expiresIn = kAccessTokenExpiresInSeconds;
        result.refreshExpiresIn = kRefreshTokenExpiresInSeconds;
        result.issuedAt = utc_now_iso8601();

        std::lock_guard<std::mutex> lock(g_tokenSessionsMutex);
        pruneExpiredTokensLocked(now);
        g_accessTokenSessions[result.accessToken] = TokenSession{user->userId, now + std::chrono::seconds(kAccessTokenExpiresInSeconds)};
        g_refreshTokenSessions[result.refreshToken] = TokenSession{user->userId, now + std::chrono::seconds(kRefreshTokenExpiresInSeconds)};
        return result;
    } catch (const std::exception& e) {
        LogSync::error("[users.login] failed to create tokens: ", e.what());
        return LoginResult{};
    }
}

VmsUserService::SignupStatus VmsUserService::signup(
    const std::string& loginId, const std::string& name, const std::string& pw) {
    if (userRepository.findByLoginId(loginId)) {
        return SignupStatus::LoginIdAlreadyExists;
    }

    std::string pwHash;
    try {
        pwHash = sha256_hex(pw);
    } catch (const std::exception& e) {
        LogSync::error("[users.signup] failed to hash password: ", e.what());
        return SignupStatus::InternalError;
    }

    if (!userRepository.createUser(loginId, name, pwHash)) {
        LogSync::error("[users.signup] users.createUser failed for login_id=", loginId);
        return SignupStatus::InternalError;
    }

    const auto createdUser = userRepository.findByLoginId(loginId);
    if (!createdUser) {
        LogSync::error("[users.signup] created user not found for login_id=", loginId);
        return SignupStatus::InternalError;
    }

    if (!userRepository.createPendingApproval(createdUser->userId)) {
        LogSync::error("[users.signup] users.createPendingApproval failed for user_id=", createdUser->userId);
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

    const auto user = userRepository.findById(userId);
    if (!user || !user->isActive) {
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
    const auto user = userRepository.findById(userId);
    if (!user) {
        UserInfoResult result;
        result.status = UserInfoStatus::NotFound;
        return result;
    }

    UserInfoResult result;
    result.status = UserInfoStatus::Success;
    result.userId = user->userId;
    result.loginId = user->loginId;
    result.name = user->name;
    result.createdAt = user->createdAt;
    result.lastLoginAt = user->lastLoginAt;
    result.roles = userRepository.findRoleCodes(userId);
    return result;
}

VmsUserService::UpdateResult VmsUserService::updateUserInfo(
    std::int64_t userId,
    const std::string& currentPassword,
    const std::string& name,
    const std::string& newPassword) {
    const auto user = userRepository.findById(userId);
    if (!user) {
        UpdateResult result;
        result.status = UpdateStatus::NotFound;
        return result;
    }

    try {
        if (!verifyPassword(*user, currentPassword)) {
            UpdateResult result;
            result.status = UpdateStatus::InvalidPassword;
            return result;
        }
    } catch (const std::exception& e) {
        LogSync::error("[users.update] failed to verify password: ", e.what());
        return UpdateResult{};
    }

    const bool updateName = !name.empty();
    const bool updatePassword = !newPassword.empty();

    std::string pwHash;
    if (updatePassword) {
        try {
            pwHash = sha256_hex(newPassword);
        } catch (const std::exception& e) {
            LogSync::error("[users.update] failed to hash new password: ", e.what());
            return UpdateResult{};
        }
    }

    bool ok = true;
    if (updateName && updatePassword) {
        ok = userRepository.updateNameAndPassword(userId, name, pwHash);
    } else if (updateName) {
        ok = userRepository.updateName(userId, name);
    } else if (updatePassword) {
        ok = userRepository.updatePassword(userId, pwHash);
    }

    if (!ok) {
        LogSync::error("[users.update] update failed for user_id=", userId);
        return UpdateResult{};
    }

    const auto updatedUser = userRepository.findById(userId);
    if (!updatedUser) {
        UpdateResult result;
        result.status = UpdateStatus::NotFound;
        return result;
    }

    UpdateResult result;
    result.status = UpdateStatus::Success;
    result.userId = updatedUser->userId;
    result.loginId = updatedUser->loginId;
    result.name = updatedUser->name;
    result.updatedAt = utc_now_iso8601();
    return result;
}

VmsUserService::DeleteStatus VmsUserService::deleteUser(std::int64_t userId, const std::string& currentPassword) {
    const auto user = userRepository.findById(userId);
    if (!user) {
        return DeleteStatus::NotFound;
    }

    try {
        if (!verifyPassword(*user, currentPassword)) {
            return DeleteStatus::InvalidPassword;
        }
    } catch (const std::exception& e) {
        LogSync::error("[users.delete] failed to verify password: ", e.what());
        return DeleteStatus::InternalError;
    }

    if (!userRepository.deactivate(userId)) {
        LogSync::error("[users.delete] deactivate failed for user_id=", userId);
        return DeleteStatus::InternalError;
    }

    std::lock_guard<std::mutex> lock(g_tokenSessionsMutex);
    removeUserTokensLocked(userId);
    return DeleteStatus::Success;
}
