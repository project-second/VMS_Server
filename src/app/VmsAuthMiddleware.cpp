#include "app/VmsAuthMiddleware.h"

#include "StringHelpers.h"
#include "VmsApiResponses.h"
#include "VmsRequestHelpers.h"

#include <regex>

namespace {

// 쿼리 문자열을 제거해 요청 경로를 기준으로 인증 정책을 판단한다.
std::string requestPath(const Http::Request& req) {
    return split_once(req.target, '?')[0];
}

// HTTP 메서드는 대소문자를 구분하지 않고 비교하며, 경로는 정확히 일치해야 한다.
bool requestMatches(const Http::Request& req, const std::string& method, const std::string& path) {
    return to_lower(req.method) == to_lower(method) && requestPath(req) == path;
}

bool isHlsSegmentRequest(const Http::Request& req) {
    // 세션 ID와 세그먼트 경로 요소가 각각 한 단계인 HLS 세그먼트 GET 요청을 식별한다.
    static const std::regex route(
        R"(^/playback/sessions/[^/]+/segments/[^/]+$)");
    return to_lower(req.method) == "get" &&
        std::regex_match(requestPath(req), route);
}

// 로그인, 회원가입, 토큰 갱신 및 HLS 세그먼트 조회는 이 미들웨어의 Bearer 인증에서 제외한다.
bool isPublicHttpApi(const Http::Request& req) {
    return requestMatches(req, "POST", "/users/login")
        || requestMatches(req, "POST", "/users/signup")
        || requestMatches(req, "POST", "/users/token/refresh")
        || isHlsSegmentRequest(req);
}

bool isAdminHttpApi(const Http::Request& req) {
    // /admin/으로 시작하는 경로에 관리자 권한 검사를 적용한다.
    return requestPath(req).rfind("/admin/", 0) == 0;
}

} // namespace

VmsAuthMiddleware::VmsAuthMiddleware(VmsUserService& userService, VmsAdminService& adminService)
    : userService_(userService), adminService_(adminService) {}

std::optional<Http::Response> VmsAuthMiddleware::operator()(Http::Request& req) const {
    // 공개 경로는 토큰을 검사하지 않고 후속 요청 처리로 넘긴다.
    if (isPublicHttpApi(req)) {
        return std::nullopt;
    }

    // Bearer 액세스 토큰이 없으면 인증 실패 응답(401)을 반환한다.
    const auto accessToken = VmsRequest::extractBearerToken(req);
    if (!accessToken) {
        return VmsApiResponse::invalidTokenResponse();
    }

    // 토큰이 유효하지 않은 경우도 동일한 인증 실패 응답으로 처리한다.
    const auto validation = userService_.validateAccessToken(*accessToken);
    if (validation.status != VmsUserService::TokenStatus::Valid) {
        return VmsApiResponse::invalidTokenResponse();
    }

    // 후속 컨트롤러에서 사용할 인증 여부와 사용자 ID를 요청에 저장한다.
    req.auth.authenticated = true;
    req.auth.userId = validation.userId;

    // 관리자 API는 토큰 인증을 통과한 뒤 관리자 권한까지 확인한다.
    if (isAdminHttpApi(req)) {
        const auto adminStatus = adminService_.checkAdmin(validation.userId);
        if (adminStatus == VmsAdminService::AdminCheckStatus::Forbidden) {
            // 관리자 권한 검사가 거부되면 접근 금지 응답(403)을 반환한다.
            return VmsApiResponse::forbiddenResponse();
        }
        if (adminStatus == VmsAdminService::AdminCheckStatus::NotFound) {
            // 권한 검사에서 사용자를 찾지 못하면 인증 실패(401)로 처리한다.
            return VmsApiResponse::invalidTokenResponse();
        }
        if (adminStatus != VmsAdminService::AdminCheckStatus::Authorized) {
            // 그 밖의 권한 검사 실패는 서버 내부 오류(500)로 처리한다.
            return VmsApiResponse::internalServerErrorResponse();
        }
        // 관리자 권한이 확인된 요청에만 관리자 표시를 남긴다.
        req.auth.admin = true;
    }

    // 오류 응답이 없으면 HTTP 서버가 해당 요청의 라우트 처리를 계속한다.
    return std::nullopt;
}
