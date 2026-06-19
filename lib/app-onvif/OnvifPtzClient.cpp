#include "OnvifPtzClient.h"

#include "LogSync.h"
#include "OnvifSoapContext.h"

#include "soapH.h"

namespace {

constexpr const char* kGetNodesAction = "http://www.onvif.org/ver20/ptz/wsdl/GetNodes";
constexpr const char* kGetConfigurationsAction = "http://www.onvif.org/ver20/ptz/wsdl/GetConfigurations";
constexpr const char* kGetConfigurationOptionsAction = "http://www.onvif.org/ver20/ptz/wsdl/GetConfigurationOptions";
constexpr const char* kGetPresetsAction = "http://www.onvif.org/ver20/ptz/wsdl/GetPresets";
constexpr const char* kSetPresetAction = "http://www.onvif.org/ver20/ptz/wsdl/SetPreset";
constexpr const char* kRemovePresetAction = "http://www.onvif.org/ver20/ptz/wsdl/RemovePreset";
constexpr const char* kGotoPresetAction = "http://www.onvif.org/ver20/ptz/wsdl/GotoPreset";
constexpr const char* kContinuousMoveAction = "http://www.onvif.org/ver20/ptz/wsdl/ContinuousMove";
constexpr const char* kStopAction = "http://www.onvif.org/ver20/ptz/wsdl/Stop";

OnvifPtzPreset mapPreset(const tt__PTZPreset& preset) {
    OnvifPtzPreset result;
    if (preset.token != nullptr) {
        result.token = *preset.token;
    }
    if (preset.Name != nullptr) {
        result.name = *preset.Name;
    }
    return result;
}

std::string valueOf(const std::string* value) {
    return value != nullptr ? *value : std::string{};
}

std::optional<OnvifFloatRange> mapRange(const tt__FloatRange* range) {
    if (range == nullptr) {
        return std::nullopt;
    }
    return OnvifFloatRange{range->Min, range->Max};
}

OnvifPtzSpace1D mapSpace(const tt__Space1DDescription& space) {
    OnvifPtzSpace1D result;
    result.uri = space.URI;
    result.xRange = mapRange(space.XRange);
    return result;
}

OnvifPtzSpace2D mapSpace(const tt__Space2DDescription& space) {
    OnvifPtzSpace2D result;
    result.uri = space.URI;
    result.xRange = mapRange(space.XRange);
    result.yRange = mapRange(space.YRange);
    return result;
}

template <typename SoapSpace, typename AppSpace>
std::vector<AppSpace> mapSpaceList(const std::vector<SoapSpace*>& spaces) {
    std::vector<AppSpace> result;
    result.reserve(spaces.size());
    for (const SoapSpace* space : spaces) {
        if (space != nullptr) {
            result.push_back(mapSpace(*space));
        }
    }
    return result;
}

OnvifPtzSpaces mapSpaces(const tt__PTZSpaces* spaces) {
    OnvifPtzSpaces result;
    if (spaces == nullptr) {
        return result;
    }
    result.absolutePanTiltPosition = mapSpaceList<tt__Space2DDescription, OnvifPtzSpace2D>(
        spaces->AbsolutePanTiltPositionSpace);
    result.absoluteZoomPosition = mapSpaceList<tt__Space1DDescription, OnvifPtzSpace1D>(
        spaces->AbsoluteZoomPositionSpace);
    result.relativePanTiltTranslation = mapSpaceList<tt__Space2DDescription, OnvifPtzSpace2D>(
        spaces->RelativePanTiltTranslationSpace);
    result.relativeZoomTranslation = mapSpaceList<tt__Space1DDescription, OnvifPtzSpace1D>(
        spaces->RelativeZoomTranslationSpace);
    result.continuousPanTiltVelocity = mapSpaceList<tt__Space2DDescription, OnvifPtzSpace2D>(
        spaces->ContinuousPanTiltVelocitySpace);
    result.continuousZoomVelocity = mapSpaceList<tt__Space1DDescription, OnvifPtzSpace1D>(
        spaces->ContinuousZoomVelocitySpace);
    result.panTiltSpeed = mapSpaceList<tt__Space1DDescription, OnvifPtzSpace1D>(spaces->PanTiltSpeedSpace);
    result.zoomSpeed = mapSpaceList<tt__Space1DDescription, OnvifPtzSpace1D>(spaces->ZoomSpeedSpace);
    return result;
}

OnvifPtzNode mapNode(const tt__PTZNode& node) {
    OnvifPtzNode result;
    result.token = node.token;
    result.name = valueOf(node.Name);
    result.supportedSpaces = mapSpaces(node.SupportedPTZSpaces);
    result.maximumNumberOfPresets = node.MaximumNumberOfPresets;
    result.homeSupported = node.HomeSupported;
    result.auxiliaryCommands = node.AuxiliaryCommands;
    if (node.FixedHomePosition != nullptr) {
        result.fixedHomePosition = *node.FixedHomePosition;
    }
    if (node.GeoMove != nullptr) {
        result.geoMove = *node.GeoMove;
    }
    if (node.Extension != nullptr && node.Extension->SupportedPresetTour != nullptr) {
        result.maximumNumberOfPresetTours = node.Extension->SupportedPresetTour->MaximumNumberOfPresetTours;
    }
    return result;
}

OnvifPtzConfiguration mapConfiguration(const tt__PTZConfiguration& configuration) {
    OnvifPtzConfiguration result;
    result.token = configuration.token;
    result.name = configuration.Name;
    result.useCount = configuration.UseCount;
    result.nodeToken = configuration.NodeToken;
    result.defaultAbsolutePanTiltPositionSpace = valueOf(configuration.DefaultAbsolutePantTiltPositionSpace);
    result.defaultAbsoluteZoomPositionSpace = valueOf(configuration.DefaultAbsoluteZoomPositionSpace);
    result.defaultRelativePanTiltTranslationSpace = valueOf(configuration.DefaultRelativePanTiltTranslationSpace);
    result.defaultRelativeZoomTranslationSpace = valueOf(configuration.DefaultRelativeZoomTranslationSpace);
    result.defaultContinuousPanTiltVelocitySpace = valueOf(configuration.DefaultContinuousPanTiltVelocitySpace);
    result.defaultContinuousZoomVelocitySpace = valueOf(configuration.DefaultContinuousZoomVelocitySpace);
    result.defaultPtzTimeout = valueOf(configuration.DefaultPTZTimeout);
    if (configuration.MoveRamp != nullptr) {
        result.moveRamp = *configuration.MoveRamp;
    }
    if (configuration.PresetRamp != nullptr) {
        result.presetRamp = *configuration.PresetRamp;
    }
    if (configuration.PresetTourRamp != nullptr) {
        result.presetTourRamp = *configuration.PresetTourRamp;
    }
    return result;
}

OnvifPtzConfigurationOptions mapConfigurationOptions(const tt__PTZConfigurationOptions& options) {
    OnvifPtzConfigurationOptions result;
    result.spaces = mapSpaces(options.Spaces);
    if (options.PTZTimeout != nullptr) {
        result.ptzTimeoutMin = options.PTZTimeout->Min;
        result.ptzTimeoutMax = options.PTZTimeout->Max;
    }
    result.ptzRamps = valueOf(options.PTZRamps);
    return result;
}

bool applySecurity(soap* context, const OnvifPtzClientOptions& options, const char* operationName) {
    if (applyOnvifWsSecurity(context, options.wsSecurity)) {
        return true;
    }

    LogSync::error("[ONVIF] ", operationName, " failed: unable to apply WS-Security");
    return false;
}

// ONVIF ContinuousMove는 pan/tilt와 zoom 속도를 tt__PTZSpeed 안의 선택 필드로 보낸다.
// 0 값인 축은 요청에서 생략해 장비가 기존 이동 상태를 유지할 수 있게 한다.
tt__PTZSpeed* makeVelocity(soap* context, float pan, float tilt, float zoom) {
    tt__PTZSpeed* velocity = soap_new_tt__PTZSpeed(context);
    if (velocity == nullptr) {
        return nullptr;
    }
    velocity->soap_default(context);

    if (pan != 0.0f || tilt != 0.0f) {
        tt__Vector2D* panTilt = soap_new_tt__Vector2D(context);
        if (panTilt == nullptr) {
            return nullptr;
        }
        panTilt->soap_default(context);
        panTilt->x = pan;
        panTilt->y = tilt;
        velocity->PanTilt = panTilt;
    }

    if (zoom != 0.0f) {
        tt__Vector1D* zoomVector = soap_new_tt__Vector1D(context);
        if (zoomVector == nullptr) {
            return nullptr;
        }
        zoomVector->soap_default(context);
        zoomVector->x = zoom;
        velocity->Zoom = zoomVector;
    }

    return velocity;
}

} // namespace

OnvifPtzClient::OnvifPtzClient(OnvifPtzClientOptions options)
    : options_(options) {}

std::optional<std::vector<OnvifPtzNode>> OnvifPtzClient::getNodes(const std::string& ptzServiceUrl) const {
    if (ptzServiceUrl.empty()) {
        return std::nullopt;
    }

    auto context = makeOnvifSoapContext(options_);

    _tptz__GetNodes request;
    _tptz__GetNodesResponse response;

    if (!applySecurity(context.get(), options_, "GetNodes")) {
        return std::nullopt;
    }

    const int result = soap_call___tptz__GetNodes(
        context.get(),
        ptzServiceUrl.c_str(),
        kGetNodesAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] GetNodes failed: ", onvifSoapFaultString(context.get()));
        return std::nullopt;
    }

    std::vector<OnvifPtzNode> nodes;
    nodes.reserve(response.PTZNode.size());
    for (const tt__PTZNode* node : response.PTZNode) {
        if (node != nullptr) {
            nodes.push_back(mapNode(*node));
        }
    }
    return nodes;
}

std::optional<std::vector<OnvifPtzConfiguration>> OnvifPtzClient::getConfigurations(
    const std::string& ptzServiceUrl) const {
    if (ptzServiceUrl.empty()) {
        return std::nullopt;
    }

    auto context = makeOnvifSoapContext(options_);

    _tptz__GetConfigurations request;
    _tptz__GetConfigurationsResponse response;

    if (!applySecurity(context.get(), options_, "GetConfigurations")) {
        return std::nullopt;
    }

    const int result = soap_call___tptz__GetConfigurations(
        context.get(),
        ptzServiceUrl.c_str(),
        kGetConfigurationsAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] GetConfigurations failed: ", onvifSoapFaultString(context.get()));
        return std::nullopt;
    }

    std::vector<OnvifPtzConfiguration> configurations;
    configurations.reserve(response.PTZConfiguration.size());
    for (const tt__PTZConfiguration* configuration : response.PTZConfiguration) {
        if (configuration != nullptr) {
            configurations.push_back(mapConfiguration(*configuration));
        }
    }
    return configurations;
}

std::optional<OnvifPtzConfigurationOptions> OnvifPtzClient::getConfigurationOptions(
    const std::string& ptzServiceUrl,
    const std::string& configurationToken) const {
    if (ptzServiceUrl.empty() || configurationToken.empty()) {
        return std::nullopt;
    }

    auto context = makeOnvifSoapContext(options_);

    _tptz__GetConfigurationOptions request;
    _tptz__GetConfigurationOptionsResponse response;
    request.ConfigurationToken = configurationToken;

    if (!applySecurity(context.get(), options_, "GetConfigurationOptions")) {
        return std::nullopt;
    }

    const int result = soap_call___tptz__GetConfigurationOptions(
        context.get(),
        ptzServiceUrl.c_str(),
        kGetConfigurationOptionsAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] GetConfigurationOptions failed: ", onvifSoapFaultString(context.get()));
        return std::nullopt;
    }

    if (response.PTZConfigurationOptions == nullptr) {
        LogSync::error("[ONVIF] GetConfigurationOptions failed: empty PTZConfigurationOptions response");
        return std::nullopt;
    }

    return mapConfigurationOptions(*response.PTZConfigurationOptions);
}

std::optional<std::vector<OnvifPtzPreset>> OnvifPtzClient::getPresets(
    const std::string& ptzServiceUrl,
    const std::string& profileToken) const {
    if (ptzServiceUrl.empty() || profileToken.empty()) {
        return std::nullopt;
    }

    auto context = makeOnvifSoapContext(options_);

    _tptz__GetPresets request;
    _tptz__GetPresetsResponse response;
    request.ProfileToken = profileToken;

    if (!applySecurity(context.get(), options_, "GetPresets")) {
        return std::nullopt;
    }

    const int result = soap_call___tptz__GetPresets(
        context.get(),
        ptzServiceUrl.c_str(),
        kGetPresetsAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] GetPresets failed: ", onvifSoapFaultString(context.get()));
        return std::nullopt;
    }

    std::vector<OnvifPtzPreset> presets;
    presets.reserve(response.Preset.size());
    for (const tt__PTZPreset* preset : response.Preset) {
        if (preset != nullptr) {
            presets.push_back(mapPreset(*preset));
        }
    }
    return presets;
}

std::optional<std::string> OnvifPtzClient::setPreset(
    const std::string& ptzServiceUrl,
    const std::string& profileToken,
    const std::string& presetName,
    const std::string& presetToken) const {
    if (ptzServiceUrl.empty() || profileToken.empty()) {
        return std::nullopt;
    }

    auto context = makeOnvifSoapContext(options_);

    _tptz__SetPreset request;
    _tptz__SetPresetResponse response;
    request.ProfileToken = profileToken;

    std::string nameValue = presetName;
    if (!nameValue.empty()) {
        request.PresetName = &nameValue;
    }

    tt__ReferenceToken tokenValue = presetToken;
    if (!tokenValue.empty()) {
        request.PresetToken = &tokenValue;
    }

    if (!applySecurity(context.get(), options_, "SetPreset")) {
        return std::nullopt;
    }

    const int result = soap_call___tptz__SetPreset(
        context.get(),
        ptzServiceUrl.c_str(),
        kSetPresetAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] SetPreset failed: ", onvifSoapFaultString(context.get()));
        return std::nullopt;
    }

    return response.PresetToken;
}

bool OnvifPtzClient::gotoPreset(
    const std::string& ptzServiceUrl,
    const std::string& profileToken,
    const std::string& presetToken) const {
    if (ptzServiceUrl.empty() || profileToken.empty() || presetToken.empty()) {
        return false;
    }

    auto context = makeOnvifSoapContext(options_);

    _tptz__GotoPreset request;
    _tptz__GotoPresetResponse response;
    request.ProfileToken = profileToken;
    request.PresetToken = presetToken;

    if (!applySecurity(context.get(), options_, "GotoPreset")) {
        return false;
    }

    const int result = soap_call___tptz__GotoPreset(
        context.get(),
        ptzServiceUrl.c_str(),
        kGotoPresetAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] GotoPreset failed: ", onvifSoapFaultString(context.get()));
        return false;
    }

    return true;
}

bool OnvifPtzClient::removePreset(
    const std::string& ptzServiceUrl,
    const std::string& profileToken,
    const std::string& presetToken) const {
    if (ptzServiceUrl.empty() || profileToken.empty() || presetToken.empty()) {
        return false;
    }

    auto context = makeOnvifSoapContext(options_);

    _tptz__RemovePreset request;
    _tptz__RemovePresetResponse response;
    request.ProfileToken = profileToken;
    request.PresetToken = presetToken;

    if (!applySecurity(context.get(), options_, "RemovePreset")) {
        return false;
    }

    const int result = soap_call___tptz__RemovePreset(
        context.get(),
        ptzServiceUrl.c_str(),
        kRemovePresetAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] RemovePreset failed: ", onvifSoapFaultString(context.get()));
        return false;
    }

    return true;
}

bool OnvifPtzClient::continuousMove(
    const std::string& ptzServiceUrl,
    const std::string& profileToken,
    float pan,
    float tilt,
    float zoom) const {
    if (ptzServiceUrl.empty() || profileToken.empty()) {
        return false;
    }
    if (pan == 0.0f && tilt == 0.0f && zoom == 0.0f) {
        LogSync::error("[ONVIF] ContinuousMove failed: empty velocity");
        return false;
    }

    auto context = makeOnvifSoapContext(options_);

    tt__PTZSpeed* velocity = makeVelocity(context.get(), pan, tilt, zoom);
    if (velocity == nullptr) {
        LogSync::error("[ONVIF] ContinuousMove failed: unable to allocate velocity");
        return false;
    }

    _tptz__ContinuousMove request;
    _tptz__ContinuousMoveResponse response;
    request.ProfileToken = profileToken;
    request.Velocity = velocity;

    if (!applySecurity(context.get(), options_, "ContinuousMove")) {
        return false;
    }

    const int result = soap_call___tptz__ContinuousMove(
        context.get(),
        ptzServiceUrl.c_str(),
        kContinuousMoveAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] ContinuousMove failed: ", onvifSoapFaultString(context.get()));
        return false;
    }

    return true;
}

bool OnvifPtzClient::stop(
    const std::string& ptzServiceUrl,
    const std::string& profileToken,
    bool panTilt,
    bool zoom) const {
    if (ptzServiceUrl.empty() || profileToken.empty()) {
        return false;
    }

    auto context = makeOnvifSoapContext(options_);

    _tptz__Stop request;
    _tptz__StopResponse response;
    request.ProfileToken = profileToken;
    request.PanTilt = &panTilt;
    request.Zoom = &zoom;

    if (!applySecurity(context.get(), options_, "Stop")) {
        return false;
    }

    const int result = soap_call___tptz__Stop(
        context.get(),
        ptzServiceUrl.c_str(),
        kStopAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] Stop failed: ", onvifSoapFaultString(context.get()));
        return false;
    }

    return true;
}
