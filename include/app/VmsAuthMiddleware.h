#ifndef VMS_AUTH_MIDDLEWARE_H
#define VMS_AUTH_MIDDLEWARE_H

#include "HttpServer.h"
#include "VmsAdminService.h"
#include "VmsUserService.h"

#include <optional>

class VmsAuthMiddleware {
public:
    VmsAuthMiddleware(VmsUserService& userService, VmsAdminService& adminService);

    std::optional<Http::Response> operator()(Http::Request& req) const;

private:
    VmsUserService& userService_;
    VmsAdminService& adminService_;
};

#endif // VMS_AUTH_MIDDLEWARE_H
