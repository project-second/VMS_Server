#ifndef ONVIF_PTZ_CLIENT_H
#define ONVIF_PTZ_CLIENT_H

#include "OnvifClientOptions.h"

#include <optional>
#include <string>
#include <vector>

using OnvifPtzClientOptions = OnvifClientOptions;

// ONVIF PTZ preset 정보를 애플리케이션에서 쓰기 쉬운 형태로 옮긴 DTO다.
// token은 GotoPreset/RemovePreset 호출에 사용하고, name은 UI 표시용으로 사용한다.
struct OnvifPtzPreset {
    std::string token;
    std::string name;
};

struct OnvifFloatRange {
    float min = 0.0f;
    float max = 0.0f;
};

struct OnvifPtzSpace1D {
    std::string uri;
    std::optional<OnvifFloatRange> xRange;
};

struct OnvifPtzSpace2D {
    std::string uri;
    std::optional<OnvifFloatRange> xRange;
    std::optional<OnvifFloatRange> yRange;
};

struct OnvifPtzSpaces {
    std::vector<OnvifPtzSpace2D> absolutePanTiltPosition;
    std::vector<OnvifPtzSpace1D> absoluteZoomPosition;
    std::vector<OnvifPtzSpace2D> relativePanTiltTranslation;
    std::vector<OnvifPtzSpace1D> relativeZoomTranslation;
    std::vector<OnvifPtzSpace2D> continuousPanTiltVelocity;
    std::vector<OnvifPtzSpace1D> continuousZoomVelocity;
    std::vector<OnvifPtzSpace1D> panTiltSpeed;
    std::vector<OnvifPtzSpace1D> zoomSpeed;
};

struct OnvifPtzNode {
    std::string token;
    std::string name;
    OnvifPtzSpaces supportedSpaces;
    int maximumNumberOfPresets = 0;
    bool homeSupported = false;
    std::vector<std::string> auxiliaryCommands;
    std::optional<bool> fixedHomePosition;
    std::optional<bool> geoMove;
    std::optional<int> maximumNumberOfPresetTours;
};

struct OnvifPtzConfiguration {
    std::string token;
    std::string name;
    int useCount = 0;
    std::string nodeToken;
    std::string defaultAbsolutePanTiltPositionSpace;
    std::string defaultAbsoluteZoomPositionSpace;
    std::string defaultRelativePanTiltTranslationSpace;
    std::string defaultRelativeZoomTranslationSpace;
    std::string defaultContinuousPanTiltVelocitySpace;
    std::string defaultContinuousZoomVelocitySpace;
    std::string defaultPtzTimeout;
    std::optional<int> moveRamp;
    std::optional<int> presetRamp;
    std::optional<int> presetTourRamp;
};

struct OnvifPtzConfigurationOptions {
    OnvifPtzSpaces spaces;
    std::string ptzTimeoutMin;
    std::string ptzTimeoutMax;
    std::string ptzRamps;
};

struct OnvifPtzVector {
    std::optional<float> pan;
    std::optional<float> tilt;
    std::optional<float> zoom;
    std::string panTiltSpace;
    std::string zoomSpace;
};

struct OnvifPtzStatus {
    OnvifPtzVector position;
    std::string panTiltMoveStatus;
    std::string zoomMoveStatus;
    std::string error;
};

enum class OnvifPtzOperationStatus {
    Success,
    InvalidArgument,
    Unsupported,
    UpstreamError
};

enum class OnvifPtzPresetTourOperation {
    Start,
    Stop,
    Pause,
    Extended
};

struct OnvifPtzPresetTourSpot {
    std::string presetToken;
    std::optional<float> panSpeed;
    std::optional<float> tiltSpeed;
    std::optional<float> zoomSpeed;
    std::string stayTime;
};

struct OnvifPtzPresetTour {
    std::string token;
    std::string name;
    std::string state;
    bool autoStart = false;
    std::optional<int> recurringTime;
    std::string recurringDuration;
    std::string direction;
    std::optional<bool> randomPresetOrder;
    std::vector<OnvifPtzPresetTourSpot> spots;
};

struct OnvifPtzPresetTourOptions {
    bool autoStartSupported = false;
    std::optional<OnvifFloatRange> panTiltPositionXRange;
    std::optional<OnvifFloatRange> panTiltPositionYRange;
    std::optional<OnvifFloatRange> zoomPositionRange;
    std::string stayTimeMin;
    std::string stayTimeMax;
    std::vector<std::string> presetTokens;
    std::optional<bool> homeSupported;
    std::vector<std::string> directions;
    std::optional<int> recurringTimeMin;
    std::optional<int> recurringTimeMax;
    std::string recurringDurationMin;
    std::string recurringDurationMax;
};

// ONVIF PTZ Service를 호출하는 얇은 client wrapper다.
//
// Device Service의 GetCapabilities에서 얻은 ptzXAddr를 endpoint로 받고,
// Media Service의 GetProfiles에서 얻은 profile token을 ProfileToken으로 사용한다.
// 실제 SOAP 직렬화/역직렬화와 HTTP 호출 함수는 gSOAP가 생성한 tptz stub을 사용한다.
class OnvifPtzClient {
public:
    explicit OnvifPtzClient(OnvifPtzClientOptions options = {});

    // ptzServiceUrl의 PTZ Service에 GetNodes를 호출한다.
    std::optional<std::vector<OnvifPtzNode>> getNodes(const std::string& ptzServiceUrl) const;

    // ptzServiceUrl의 PTZ Service에 GetConfigurations를 호출한다.
    std::optional<std::vector<OnvifPtzConfiguration>> getConfigurations(const std::string& ptzServiceUrl) const;

    // configurationToken으로 GetConfigurationOptions를 호출한다.
    std::optional<OnvifPtzConfigurationOptions> getConfigurationOptions(
        const std::string& ptzServiceUrl,
        const std::string& configurationToken) const;

    // profileToken에 해당하는 현재 PTZ 위치와 pan/tilt/zoom 이동 상태를 조회한다.
    std::optional<OnvifPtzStatus> getStatus(
        const std::string& ptzServiceUrl,
        const std::string& profileToken) const;

    // 장비의 preset tour 목록을 조회한다.
    std::optional<std::vector<OnvifPtzPresetTour>> getPresetTours(
        const std::string& ptzServiceUrl,
        const std::string& profileToken) const;

    // presetTourToken 하나에 대한 상세 tour 정보를 조회한다.
    std::optional<OnvifPtzPresetTour> getPresetTour(
        const std::string& ptzServiceUrl,
        const std::string& profileToken,
        const std::string& presetTourToken) const;

    // tour 생성/수정 시 장비가 허용하는 preset token, dwell time, 방향 범위를 조회한다.
    std::optional<OnvifPtzPresetTourOptions> getPresetTourOptions(
        const std::string& ptzServiceUrl,
        const std::string& profileToken,
        const std::string& presetTourToken = {}) const;

    // 빈 preset tour를 장비에 생성하고 장비가 반환한 tour token을 돌려준다.
    std::optional<std::string> createPresetTour(
        const std::string& ptzServiceUrl,
        const std::string& profileToken) const;

    // tour token, 이름, 반복 조건, spot 목록을 장비에 반영한다.
    OnvifPtzOperationStatus modifyPresetTour(
        const std::string& ptzServiceUrl,
        const std::string& profileToken,
        const OnvifPtzPresetTour& tour) const;

    // Start/Stop/Pause/Extended tour operation을 장비에 전달한다.
    OnvifPtzOperationStatus operatePresetTour(
        const std::string& ptzServiceUrl,
        const std::string& profileToken,
        const std::string& presetTourToken,
        OnvifPtzPresetTourOperation operation) const;

    // presetTourToken에 해당하는 preset tour를 장비에서 삭제한다.
    OnvifPtzOperationStatus removePresetTour(
        const std::string& ptzServiceUrl,
        const std::string& profileToken,
        const std::string& presetTourToken) const;

    // ptzServiceUrl의 PTZ Service에 GetPresets를 호출한다.
    // profileToken은 PTZ 제어 대상 Media Profile token이다.
    // URL/token이 비어 있거나 SOAP fault/transport 오류가 발생하면 std::nullopt를 반환한다.
    std::optional<std::vector<OnvifPtzPreset>> getPresets(
        const std::string& ptzServiceUrl,
        const std::string& profileToken) const;

    // 현재 PTZ 위치를 preset으로 저장한다.
    // presetToken을 비워두면 장비가 새 preset token을 생성하고, 값을 넘기면 해당 preset을 갱신한다.
    // 성공하면 장비가 반환한 preset token을 반환한다.
    std::optional<std::string> setPreset(
        const std::string& ptzServiceUrl,
        const std::string& profileToken,
        const std::string& presetName,
        const std::string& presetToken = {}) const;

    // presetToken으로 저장된 위치로 이동한다.
    // 장비별로 이동 속도와 도달 동작은 PTZ configuration에 의존한다.
    bool gotoPreset(
        const std::string& ptzServiceUrl,
        const std::string& profileToken,
        const std::string& presetToken) const;

    // presetToken에 해당하는 preset을 장비에서 삭제한다.
    bool removePreset(
        const std::string& ptzServiceUrl,
        const std::string& profileToken,
        const std::string& presetToken) const;

    // ContinuousMove를 호출해 pan/tilt/zoom 연속 이동을 시작한다.
    // pan, tilt, zoom 값은 장비의 velocity space에 맞는 속도 값이다.
    // 일반적으로 -1.0~1.0 범위를 먼저 사용하지만, 실제 허용 범위는 장비의 PTZ options를 확인해야 한다.
    // 세 값이 모두 0이면 이동 요청을 보내지 않고 false를 반환한다.
    bool continuousMove(
        const std::string& ptzServiceUrl,
        const std::string& profileToken,
        float pan,
        float tilt,
        float zoom = 0.0f) const;

    // AbsoluteMove를 호출해 지정한 절대 pan/tilt/zoom 위치로 이동한다.
    // 값의 유효 범위와 space는 장비의 PTZ configuration/options 응답을 기준으로 맞춰야 한다.
    bool absoluteMove(
        const std::string& ptzServiceUrl,
        const std::string& profileToken,
        float pan,
        float tilt,
        float zoom = 0.0f) const;

    // RelativeMove를 호출해 현재 위치 기준 pan/tilt/zoom translation만큼 이동한다.
    // 세 값이 모두 0이면 이동 요청을 보내지 않고 false를 반환한다.
    bool relativeMove(
        const std::string& ptzServiceUrl,
        const std::string& profileToken,
        float pan,
        float tilt,
        float zoom = 0.0f) const;

    // 진행 중인 PTZ 이동을 멈춘다.
    // panTilt/zoom 플래그로 어떤 축의 이동을 멈출지 지정한다.
    bool stop(
        const std::string& ptzServiceUrl,
        const std::string& profileToken,
        bool panTilt = true,
        bool zoom = true) const;

private:
    OnvifPtzClientOptions options_;
};

#endif // ONVIF_PTZ_CLIENT_H
