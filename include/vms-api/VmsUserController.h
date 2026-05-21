#ifndef __VMSUSERCONTROLLER_H__
#define __VMSUSERCONTROLLER_H__

#include "LogSync.h"
#include "HttpServer.h"

#include "VmsUserService.h"

#include "nlohmann/json.hpp"

using json = nlohmann::json;

class VmsUserController {
private:
    HttpServer& httpServer;
    VmsUserService& userService;

public:
    VmsUserController(HttpServer& server, VmsUserService& userService);
    ~VmsUserController();

private:
    void handleUserLogin();

    void handleUserLogout();

    void handleUserSignup();

    void handleGetUserInfo();

    void handleUpdateUserInfo();

    void handleDeleteUser();

    void handleRefreshToken();
};

#endif // __VMSUSERCONTROLLER_H__