#ifndef __VMSDEVICECONTROLLER_H__
#define __VMSDEVICECONTROLLER_H__

#include "LogSync.h"
#include "HttpServer.h"

#include "VmsDeviceService.h"

#include "nlohmann/json.hpp"

using json = nlohmann::json;

// VMS 장비/채널/PTZ 관련 HTTP route를 등록하는 controller다.
//
// 일부 PTZ/preset handler는 아직 API skeleton이며, 추후 채널의 ONVIF profileToken,
// ptzXAddr, 장비 인증 정보를 조회한 뒤 OnvifPtzClient와 연결해야 한다.
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
    // pan/tilt/linear는 이후 OnvifPtzClient::continuousMove로 연결하고,
    // stop은 OnvifPtzClient::stop으로 연결한다.
    void handleChannelPanControl();
    void handleChannelTiltControl();
    // void handleChannelZoomControl();
    // void handleChannelFocusControl();
    void handleChannelLinearControl();
    void handleChannelStopControl();

    // 채널 기준 PTZ preset API route.
    // ONVIF preset token과 VMS preset id 간 매핑 정책이 필요하다.
    void handleGetChannelPresetList();
    void handleSetChannelPreset();
    void handleGoToChannelPreset();
    void handleUpdateChannelPreset();
    void handleDeleteChannelPreset();

    // 채널 기준 preset tour API route.
    // Preset tour는 아직 app-onvif에 wrapper가 없으므로 추후 OnvifPtzClient 확장이 필요하다.
    void handleGetChannelTourList();
    void handleCreateChannelTour();
    void handleGetChannelTourInfo();
    void handleStartChannelTour();
    void handleStopChannelTour();
    void handleUpdateChannelTourInfo();
    void handleDeleteChannelTour();
};

#endif // __VMSDEVICECONTROLLER_H__
