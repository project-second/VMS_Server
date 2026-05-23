#ifndef __VMSEVENTCONTROLLER_H__
#define __VMSEVENTCONTROLLER_H__

#include "LogSync.h"
#include "HttpServer.h"

#include "nlohmann/json.hpp"

using json = nlohmann::json;

class VmsEventController {
private:
    HttpServer& httpServer;

public:
    VmsEventController(HttpServer& server);
    ~VmsEventController();

    void handleGetEventList();
    void handleGetEventDetails();
    void handleEventSubscription();
};

#endif // __VMSEVENTCONTROLLER_H__