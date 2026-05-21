#ifndef __VMSUSERSERVICE_H__
#define __VMSUSERSERVICE_H__

#include <cstdint>
#include <string>

class VmsUserService {
private:
    enum class LoginStatus {
        SUCCESS,
        INVALID_CREDENTIALS,
        ACCOUNT_LOCKED,
        INTERNAL_ERROR,
        UNKNOWN_ERROR
    };

    struct LoginResult {
        LoginStatus status = LoginStatus::INTERNAL_ERROR;
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

public:
    VmsUserService();
    ~VmsUserService();

    LoginResult login(const std::string& loginId, const std::string& pw);

    SignupStatus signup(const std::string& loginId, const std::string& name, const std::string& pw);

    LogoutStatus logout(const std::string& accessToken, const std::string& refreshToken);

    TokenValidationResult validateAccessToken(const std::string& accessToken);

    TokenValidationResult validateRefreshToken(const std::string& refreshToken);
};

#endif // __VMSUSERSERVICE_H__