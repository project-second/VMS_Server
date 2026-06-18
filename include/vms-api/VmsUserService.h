#ifndef __VMSUSERSERVICE_H__
#define __VMSUSERSERVICE_H__

#include <cstdint>
#include <string>
#include <vector>

class VmsUserRepository;

class VmsUserService {
public:
    enum class LoginStatus {
        Success,
        InvalidCredentials,
        PendingApproval,
        Rejected,
        Inactive,
        InternalError
    };

    struct LoginResult {
        LoginStatus status = LoginStatus::InternalError;
        std::string accessToken;
        std::string refreshToken;
        std::int64_t expiresIn = 0;
        std::int64_t refreshExpiresIn = 0;
        std::string issuedAt;
    };

    enum class SignupStatus {
        Created,
        LoginIdAlreadyExists,
        InternalError
    };

    enum class LogoutStatus {
        Success,
        InvalidToken
    };

    enum class TokenStatus {
        Valid,
        Invalid
    };

    struct TokenValidationResult {
        TokenStatus status = TokenStatus::Invalid;
        std::int64_t userId = 0;
    };

    enum class RefreshStatus {
        Success,
        InvalidRefreshToken,
        InternalError
    };

    struct RefreshResult {
        RefreshStatus status = RefreshStatus::InternalError;
        std::string accessToken;
        std::string refreshToken;
        std::int64_t expiresIn = 0;
        std::int64_t refreshExpiresIn = 0;
        std::string issuedAt;
    };

    enum class UserInfoStatus {
        Success,
        NotFound,
        InternalError
    };

    struct UserInfoResult {
        UserInfoStatus status = UserInfoStatus::InternalError;
        std::int64_t userId = 0;
        std::string loginId;
        std::string name;
        std::vector<std::string> roles;
        std::string createdAt;
        std::string lastLoginAt;
    };

    enum class UpdateStatus {
        Success,
        InvalidPassword,
        NotFound,
        InternalError
    };

    struct UpdateResult {
        UpdateStatus status = UpdateStatus::InternalError;
        std::int64_t userId = 0;
        std::string loginId;
        std::string name;
        std::string updatedAt;
    };

    enum class DeleteStatus {
        Success,
        InvalidPassword,
        NotFound,
        InternalError
    };

public:
    VmsUserService();
    explicit VmsUserService(VmsUserRepository& userRepository);
    ~VmsUserService();

    LoginResult login(const std::string& loginId, const std::string& pw);
    SignupStatus signup(const std::string& loginId, const std::string& name, const std::string& pw);
    LogoutStatus logout(const std::string& accessToken, const std::string& refreshToken);
    RefreshResult refreshToken(const std::string& refreshToken);
    TokenValidationResult validateAccessToken(const std::string& accessToken) const;
    TokenValidationResult validateRefreshToken(const std::string& refreshToken) const;
    UserInfoResult getUserInfo(std::int64_t userId);
    UpdateResult updateUserInfo(std::int64_t userId,
        const std::string& currentPassword,
        const std::string& name,
        const std::string& newPassword);
    DeleteStatus deleteUser(std::int64_t userId, const std::string& currentPassword);

private:
    VmsUserRepository& userRepository;
};

#endif // __VMSUSERSERVICE_H__
