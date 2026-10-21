#ifndef VMS_AUTH_MIDDLEWARE_H
#define VMS_AUTH_MIDDLEWARE_H

#include "HttpServer.h"
#include "VmsAdminService.h"
#include "VmsUserService.h"

#include <optional>

/**
 * @brief HTTP 요청의 액세스 토큰을 검증하고 관리자 API에 대한 추가 권한 검사를 수행한다.
 */
class VmsAuthMiddleware {
public:
    /**
     * @brief 인증과 관리자 권한 검사에 사용할 서비스를 연결한다.
     *
     * 전달받은 서비스는 소유하지 않으며, 미들웨어보다 오래 유지되어야 한다.
     * @param userService 액세스 토큰 검증을 담당하는 서비스.
     * @param adminService 관리자 권한 확인을 담당하는 서비스.
     */
    VmsAuthMiddleware(VmsUserService& userService, VmsAdminService& adminService);

    /**
     * @brief 공개 경로를 제외한 요청을 인증하고 관리자 경로의 접근 권한을 확인한다.
     * @param req 검증된 사용자 ID와 관리자 권한 확인 결과를 기록할 HTTP 요청.
     * @return 요청 처리를 계속하면 std::nullopt, 검사를 통과하지 못하면 반환할 오류 응답.
     */
    std::optional<Http::Response> operator()(Http::Request& req) const;

private:
    VmsUserService& userService_; ///< 액세스 토큰 검증을 담당하는 외부 서비스 참조.
    VmsAdminService& adminService_; ///< 관리자 권한 확인을 담당하는 외부 서비스 참조.
};

#endif // VMS_AUTH_MIDDLEWARE_H
