#ifndef __VMSADMINCONTROLLER_H__
#define __VMSADMINCONTROLLER_H__

#include "LogSync.h"
#include "HttpServer.h"

#include "VmsAdminService.h"

#include "nlohmann/json.hpp"

using json = nlohmann::json;

class VmsAdminController {
private:
    HttpServer& httpServer;
    VmsAdminService& adminService;

public:
    VmsAdminController(HttpServer& server, VmsAdminService& adminService);
    ~VmsAdminController();

private:
    void handleGetUserApprovals();
    void handleApproveUser();
    void handleRejectUser();
    void handleGetUsersRole();
    void handleAddUserRole();
    void handleDeleteUserRole();
    void handleUserDisactivation();
};

#endif // __VMSADMINCONTROLLER_H__
