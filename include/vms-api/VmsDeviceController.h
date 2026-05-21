#ifndef __VMSDEVICECONTROLLER_H__
#define __VMSDEVICECONTROLLER_H__

#include "LogSync.h"
#include "HttpServer.h"

#include "nlohmann/json.hpp"

using json = nlohmann::json;

class VmsDeviceController {
private:
    HttpServer& httpServer;

public:
    VmsDeviceController(HttpServer& server);
    ~VmsDeviceController();

    void handleGetDeviceList();
    void handleChangeDeviceName();
    void handleGetDeviceChannelList();
    void handleChannelPanControl();
    void handleChannelTiltControl();
    // void handleChannelZoomControl();
    // void handleChannelFocusControl();
    void handleChannelLinearControl();
    void handleChannelStopControl();
    void handleGetChannelPresetList();
    void handleSetChannelPreset();
    void handleGoToChannelPreset();
    void handleUpdateChannelPreset();
    void handleDeleteChannelPreset();
    void handleGetChannelTourList();
    void handleCreateChannelTour();
    void handleGetChannelTourInfo();
    void handleStartChannelTour();
    void handleStopChannelTour();
    void handleUpdateChannelTourInfo();
    void handleDeleteChannelTour();
};

#endif // __VMSDEVICECONTROLLER_H__