#ifndef __VMSDEVICECONTROLLER_H__
#define __VMSDEVICECONTROLLER_H__

#include "LogSync.h"
#include "HttpServer.h"

#include "VmsDeviceService.h"

#include "nlohmann/json.hpp"

using json = nlohmann::json;

// VMS 장비/채널/PTZ 관련 HTTP route를 등록하는 controller다.
//
// VMS 장비/채널/PTZ 관련 HTTP route를 등록하는 controller다.
// 채널 PTZ route는 channelId로 ONVIF profileToken, ptzXAddr, 장비 인증 정보를 조회한다.
class VmsDeviceController {
private:
    HttpServer& httpServer;
    VmsDeviceService& deviceService;

public:
    VmsDeviceController(HttpServer& server, VmsDeviceService& deviceService);
    ~VmsDeviceController();

    // 장비 목록/장비 이름/장비별 채널 목록 API route.
    void handleGetDeviceList();
    void handleChangeDeviceName();
    void handleGetDeviceChannelList();

    // 채널 기준 PTZ 이동/정지 API route.
    // pan/tilt/linear는 OnvifPtzClient::continuousMove로 연결하고,
    // stop은 OnvifPtzClient::stop으로 연결한다.
    void handleChannelPanControl();
    void handleChannelTiltControl();
    // void handleChannelZoomControl();
    // void handleChannelFocusControl();
    void handleChannelLinearControl();
    void handleChannelStopControl();

    // 채널 기준 PTZ preset API route.
    // ONVIF preset token과 VMS preset id 간 매핑은 ptz_preset cache로 처리한다.
    void handleGetChannelPresetList();
    void handleSetChannelPreset();
    void handleGoToChannelPreset();
    void handleUpdateChannelPreset();
    void handleDeleteChannelPreset();

    // 채널 기준 preset tour API route.
    // ONVIF preset tour token과 VMS tour id/item 간 매핑은 ptz_tours cache로 처리한다.
    void handleGetChannelTourList();
    void handleCreateChannelTour();
    void handleGetChannelTourInfo();
    void handleStartChannelTour();
    void handleStopChannelTour();
    void handleUpdateChannelTourInfo();
    void handleDeleteChannelTour();
};

#endif // __VMSDEVICECONTROLLER_H__
