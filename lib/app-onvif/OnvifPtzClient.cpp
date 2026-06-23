#include "OnvifPtzClient.h"

#include "LogSync.h"
#include "OnvifSoapContext.h"

#include "soapH.h"

#include <cstring>

namespace {

constexpr const char* kGetNodesAction = "http://www.onvif.org/ver20/ptz/wsdl/GetNodes";
constexpr const char* kGetConfigurationsAction = "http://www.onvif.org/ver20/ptz/wsdl/GetConfigurations";
constexpr const char* kGetConfigurationOptionsAction = "http://www.onvif.org/ver20/ptz/wsdl/GetConfigurationOptions";
constexpr const char* kGetStatusAction = "http://www.onvif.org/ver20/ptz/wsdl/GetStatus";
constexpr const char* kGetPresetToursAction = "http://www.onvif.org/ver20/ptz/wsdl/GetPresetTours";
constexpr const char* kGetPresetTourAction = "http://www.onvif.org/ver20/ptz/wsdl/GetPresetTour";
constexpr const char* kGetPresetTourOptionsAction = "http://www.onvif.org/ver20/ptz/wsdl/GetPresetTourOptions";
constexpr const char* kCreatePresetTourAction = "http://www.onvif.org/ver20/ptz/wsdl/CreatePresetTour";
constexpr const char* kModifyPresetTourAction = "http://www.onvif.org/ver20/ptz/wsdl/ModifyPresetTour";
constexpr const char* kOperatePresetTourAction = "http://www.onvif.org/ver20/ptz/wsdl/OperatePresetTour";
constexpr const char* kRemovePresetTourAction = "http://www.onvif.org/ver20/ptz/wsdl/RemovePresetTour";
constexpr const char* kGetPresetsAction = "http://www.onvif.org/ver20/ptz/wsdl/GetPresets";
constexpr const char* kSetPresetAction = "http://www.onvif.org/ver20/ptz/wsdl/SetPreset";
constexpr const char* kRemovePresetAction = "http://www.onvif.org/ver20/ptz/wsdl/RemovePreset";
constexpr const char* kGotoPresetAction = "http://www.onvif.org/ver20/ptz/wsdl/GotoPreset";
constexpr const char* kContinuousMoveAction = "http://www.onvif.org/ver20/ptz/wsdl/ContinuousMove";
constexpr const char* kRelativeMoveAction = "http://www.onvif.org/ver20/ptz/wsdl/RelativeMove";
constexpr const char* kAbsoluteMoveAction = "http://www.onvif.org/ver20/ptz/wsdl/AbsoluteMove";
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

std::string moveStatusValue(const tt__MoveStatus* status) {
    if (status == nullptr) {
        return {};
    }

    switch (*status) {
    case tt__MoveStatus::IDLE:
        return "IDLE";
    case tt__MoveStatus::MOVING:
        return "MOVING";
    case tt__MoveStatus::UNKNOWN:
        return "UNKNOWN";
    }

    return {};
}

OnvifPtzVector mapVector(const tt__PTZVector* vector) {
    OnvifPtzVector result;
    if (vector == nullptr) {
        return result;
    }

    if (vector->PanTilt != nullptr) {
        result.pan = vector->PanTilt->x;
        result.tilt = vector->PanTilt->y;
        result.panTiltSpace = valueOf(vector->PanTilt->space);
    }
    if (vector->Zoom != nullptr) {
        result.zoom = vector->Zoom->x;
        result.zoomSpace = valueOf(vector->Zoom->space);
    }
    return result;
}

OnvifPtzStatus mapStatus(const tt__PTZStatus& status) {
    OnvifPtzStatus result;
    result.position = mapVector(status.Position);
    if (status.MoveStatus != nullptr) {
        result.panTiltMoveStatus = moveStatusValue(status.MoveStatus->PanTilt);
        result.zoomMoveStatus = moveStatusValue(status.MoveStatus->Zoom);
    }
    result.error = valueOf(status.Error);
    return result;
}

std::string presetTourStateValue(tt__PTZPresetTourState state) {
    switch (state) {
    case tt__PTZPresetTourState::Idle:
        return "Idle";
    case tt__PTZPresetTourState::Touring:
        return "Touring";
    case tt__PTZPresetTourState::Paused:
        return "Paused";
    case tt__PTZPresetTourState::Extended:
        return "Extended";
    }
    return {};
}

std::string presetTourDirectionValue(tt__PTZPresetTourDirection direction) {
    switch (direction) {
    case tt__PTZPresetTourDirection::Forward:
        return "Forward";
    case tt__PTZPresetTourDirection::Backward:
        return "Backward";
    case tt__PTZPresetTourDirection::Extended:
        return "Extended";
    }
    return {};
}

std::optional<tt__PTZPresetTourDirection> presetTourDirectionFromValue(const std::string& value) {
    if (value.empty() || value == "Forward") {
        return tt__PTZPresetTourDirection::Forward;
    }
    if (value == "Backward") {
        return tt__PTZPresetTourDirection::Backward;
    }
    if (value == "Extended") {
        return tt__PTZPresetTourDirection::Extended;
    }
    return std::nullopt;
}

tt__PTZPresetTourOperation presetTourOperationValue(OnvifPtzPresetTourOperation operation) {
    switch (operation) {
    case OnvifPtzPresetTourOperation::Start:
        return tt__PTZPresetTourOperation::Start;
    case OnvifPtzPresetTourOperation::Stop:
        return tt__PTZPresetTourOperation::Stop;
    case OnvifPtzPresetTourOperation::Pause:
        return tt__PTZPresetTourOperation::Pause;
    case OnvifPtzPresetTourOperation::Extended:
        return tt__PTZPresetTourOperation::Extended;
    }
    return tt__PTZPresetTourOperation::Extended;
}

OnvifPtzPresetTourSpot mapPresetTourSpot(const tt__PTZPresetTourSpot& spot) {
    OnvifPtzPresetTourSpot result;
    if (spot.PresetDetail != nullptr &&
        spot.PresetDetail->__union_PTZPresetTourPresetDetail == SOAP_UNION__tt__union_PTZPresetTourPresetDetail_PresetToken &&
        spot.PresetDetail->union_PTZPresetTourPresetDetail.PresetToken != nullptr) {
        result.presetToken = *spot.PresetDetail->union_PTZPresetTourPresetDetail.PresetToken;
    }
    if (spot.Speed != nullptr) {
        if (spot.Speed->PanTilt != nullptr) {
            result.panSpeed = spot.Speed->PanTilt->x;
            result.tiltSpeed = spot.Speed->PanTilt->y;
        }
        if (spot.Speed->Zoom != nullptr) {
            result.zoomSpeed = spot.Speed->Zoom->x;
        }
    }
    result.stayTime = valueOf(spot.StayTime);
    return result;
}

OnvifPtzPresetTour mapPresetTour(const tt__PresetTour& tour) {
    OnvifPtzPresetTour result;
    result.token = valueOf(tour.token);
    result.name = valueOf(tour.Name);
    if (tour.Status != nullptr) {
        result.state = presetTourStateValue(tour.Status->State);
    }
    result.autoStart = tour.AutoStart;
    if (tour.StartingCondition != nullptr) {
        if (tour.StartingCondition->RecurringTime != nullptr) {
            result.recurringTime = *tour.StartingCondition->RecurringTime;
        }
        result.recurringDuration = valueOf(tour.StartingCondition->RecurringDuration);
        if (tour.StartingCondition->Direction != nullptr) {
            result.direction = presetTourDirectionValue(*tour.StartingCondition->Direction);
        }
        if (tour.StartingCondition->RandomPresetOrder != nullptr) {
            result.randomPresetOrder = *tour.StartingCondition->RandomPresetOrder;
        }
    }
    result.spots.reserve(tour.TourSpot.size());
    for (const tt__PTZPresetTourSpot* spot : tour.TourSpot) {
        if (spot != nullptr) {
            result.spots.push_back(mapPresetTourSpot(*spot));
        }
    }
    return result;
}

OnvifPtzPresetTourOptions mapPresetTourOptions(const tt__PTZPresetTourOptions& options) {
    OnvifPtzPresetTourOptions result;
    result.autoStartSupported = options.AutoStart;
    if (options.StartingCondition != nullptr) {
        if (options.StartingCondition->RecurringTime != nullptr) {
            result.recurringTimeMin = options.StartingCondition->RecurringTime->Min;
            result.recurringTimeMax = options.StartingCondition->RecurringTime->Max;
        }
        if (options.StartingCondition->RecurringDuration != nullptr) {
            result.recurringDurationMin = options.StartingCondition->RecurringDuration->Min;
            result.recurringDurationMax = options.StartingCondition->RecurringDuration->Max;
        }
        result.directions.reserve(options.StartingCondition->Direction.size());
        for (tt__PTZPresetTourDirection direction : options.StartingCondition->Direction) {
            result.directions.push_back(presetTourDirectionValue(direction));
        }
    }
    if (options.TourSpot != nullptr) {
        if (options.TourSpot->StayTime != nullptr) {
            result.stayTimeMin = options.TourSpot->StayTime->Min;
            result.stayTimeMax = options.TourSpot->StayTime->Max;
        }
        if (options.TourSpot->PresetDetail != nullptr) {
            result.presetTokens = options.TourSpot->PresetDetail->PresetToken;
            if (options.TourSpot->PresetDetail->Home != nullptr) {
                result.homeSupported = *options.TourSpot->PresetDetail->Home;
            }
            if (options.TourSpot->PresetDetail->PanTiltPositionSpace != nullptr) {
                result.panTiltPositionXRange = mapRange(options.TourSpot->PresetDetail->PanTiltPositionSpace->XRange);
                result.panTiltPositionYRange = mapRange(options.TourSpot->PresetDetail->PanTiltPositionSpace->YRange);
            }
            if (options.TourSpot->PresetDetail->ZoomPositionSpace != nullptr) {
                result.zoomPositionRange = mapRange(options.TourSpot->PresetDetail->ZoomPositionSpace->XRange);
            }
        }
    }
    return result;
}

std::string* makeSoapString(soap* context, const std::string& value) {
    std::string* result = soap_new_std__string(context);
    if (result != nullptr) {
        *result = value;
    }
    return result;
}

tt__PTZPresetTourSpot* makePresetTourSpot(soap* context, const OnvifPtzPresetTourSpot& spot) {
    if (spot.presetToken.empty()) {
        return nullptr;
    }

    tt__PTZPresetTourSpot* result = soap_new_tt__PTZPresetTourSpot(context);
    tt__PTZPresetTourPresetDetail* detail = soap_new_tt__PTZPresetTourPresetDetail(context);
    if (result == nullptr || detail == nullptr) {
        return nullptr;
    }
    result->soap_default(context);
    detail->soap_default(context);

    detail->__union_PTZPresetTourPresetDetail = SOAP_UNION__tt__union_PTZPresetTourPresetDetail_PresetToken;
    detail->union_PTZPresetTourPresetDetail.PresetToken = makeSoapString(context, spot.presetToken);
    if (detail->union_PTZPresetTourPresetDetail.PresetToken == nullptr) {
        return nullptr;
    }
    result->PresetDetail = detail;

    if (spot.panSpeed.has_value() || spot.tiltSpeed.has_value() || spot.zoomSpeed.has_value()) {
        result->Speed = soap_new_tt__PTZSpeed(context);
        if (result->Speed == nullptr) {
            return nullptr;
        }
        result->Speed->soap_default(context);
        if (spot.panSpeed.has_value() || spot.tiltSpeed.has_value()) {
            result->Speed->PanTilt = soap_new_tt__Vector2D(context);
            if (result->Speed->PanTilt == nullptr) {
                return nullptr;
            }
            result->Speed->PanTilt->soap_default(context);
            result->Speed->PanTilt->x = spot.panSpeed.value_or(0.0f);
            result->Speed->PanTilt->y = spot.tiltSpeed.value_or(0.0f);
        }
        if (spot.zoomSpeed.has_value()) {
            result->Speed->Zoom = soap_new_tt__Vector1D(context);
            if (result->Speed->Zoom == nullptr) {
                return nullptr;
            }
            result->Speed->Zoom->soap_default(context);
            result->Speed->Zoom->x = *spot.zoomSpeed;
        }
    }

    if (!spot.stayTime.empty()) {
        result->StayTime = makeSoapString(context, spot.stayTime);
        if (result->StayTime == nullptr) {
            return nullptr;
        }
    }

    return result;
}

tt__PresetTour* makePresetTour(soap* context, const OnvifPtzPresetTour& tour) {
    if (tour.token.empty()) {
        return nullptr;
    }

    tt__PresetTour* result = soap_new_tt__PresetTour(context);
    tt__PTZPresetTourStatus* status = soap_new_tt__PTZPresetTourStatus(context);
    tt__PTZPresetTourStartingCondition* startingCondition = soap_new_tt__PTZPresetTourStartingCondition(context);
    if (result == nullptr || status == nullptr || startingCondition == nullptr) {
        return nullptr;
    }
    result->soap_default(context);
    status->soap_default(context);
    startingCondition->soap_default(context);

    result->token = makeSoapString(context, tour.token);
    if (result->token == nullptr) {
        return nullptr;
    }
    if (!tour.name.empty()) {
        result->Name = makeSoapString(context, tour.name);
        if (result->Name == nullptr) {
            return nullptr;
        }
    }
    status->State = tt__PTZPresetTourState::Idle;
    result->Status = status;
    result->AutoStart = tour.autoStart;

    if (tour.recurringTime.has_value()) {
        startingCondition->RecurringTime = soap_new_int(context);
        if (startingCondition->RecurringTime == nullptr) {
            return nullptr;
        }
        *startingCondition->RecurringTime = *tour.recurringTime;
    }
    if (!tour.recurringDuration.empty()) {
        startingCondition->RecurringDuration = makeSoapString(context, tour.recurringDuration);
        if (startingCondition->RecurringDuration == nullptr) {
            return nullptr;
        }
    }
    if (!tour.direction.empty()) {
        std::optional<tt__PTZPresetTourDirection> direction = presetTourDirectionFromValue(tour.direction);
        if (!direction.has_value()) {
            return nullptr;
        }
        startingCondition->Direction = soap_new_tt__PTZPresetTourDirection(context);
        if (startingCondition->Direction == nullptr) {
            return nullptr;
        }
        *startingCondition->Direction = *direction;
    }
    if (tour.randomPresetOrder.has_value()) {
        startingCondition->RandomPresetOrder = soap_new_bool(context);
        if (startingCondition->RandomPresetOrder == nullptr) {
            return nullptr;
        }
        *startingCondition->RandomPresetOrder = *tour.randomPresetOrder;
    }
    result->StartingCondition = startingCondition;

    result->TourSpot.reserve(tour.spots.size());
    for (const OnvifPtzPresetTourSpot& spot : tour.spots) {
        tt__PTZPresetTourSpot* soapSpot = makePresetTourSpot(context, spot);
        if (soapSpot == nullptr) {
            return nullptr;
        }
        result->TourSpot.push_back(soapSpot);
    }

    return result;
}

OnvifPtzOperationStatus operationStatusFromSoapFault(soap* context, const char* operationName) {
    const char* fault = onvifSoapFaultString(context);
    LogSync::error("[ONVIF] ", operationName, " failed: ", fault);
    if (fault != nullptr &&
        (std::strstr(fault, "ActionNotSupported") != nullptr ||
         std::strstr(fault, "NotSupported") != nullptr ||
         std::strstr(fault, "Unsupported") != nullptr)) {
        return OnvifPtzOperationStatus::Unsupported;
    }
    return OnvifPtzOperationStatus::UpstreamError;
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

tt__PTZVector* makeVector(soap* context, float pan, float tilt, float zoom, bool includeZeroAxes) {
    tt__PTZVector* vector = soap_new_tt__PTZVector(context);
    if (vector == nullptr) {
        return nullptr;
    }
    vector->soap_default(context);

    if (includeZeroAxes || pan != 0.0f || tilt != 0.0f) {
        tt__Vector2D* panTilt = soap_new_tt__Vector2D(context);
        if (panTilt == nullptr) {
            return nullptr;
        }
        panTilt->soap_default(context);
        panTilt->x = pan;
        panTilt->y = tilt;
        vector->PanTilt = panTilt;
    }

    if (includeZeroAxes || zoom != 0.0f) {
        tt__Vector1D* zoomVector = soap_new_tt__Vector1D(context);
        if (zoomVector == nullptr) {
            return nullptr;
        }
        zoomVector->soap_default(context);
        zoomVector->x = zoom;
        vector->Zoom = zoomVector;
    }

    return vector;
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

std::optional<OnvifPtzStatus> OnvifPtzClient::getStatus(
    const std::string& ptzServiceUrl,
    const std::string& profileToken) const {
    if (ptzServiceUrl.empty() || profileToken.empty()) {
        return std::nullopt;
    }

    auto context = makeOnvifSoapContext(options_);

    _tptz__GetStatus request;
    _tptz__GetStatusResponse response;
    request.ProfileToken = profileToken;

    if (!applySecurity(context.get(), options_, "GetStatus")) {
        return std::nullopt;
    }

    const int result = soap_call___tptz__GetStatus(
        context.get(),
        ptzServiceUrl.c_str(),
        kGetStatusAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] GetStatus failed: ", onvifSoapFaultString(context.get()));
        return std::nullopt;
    }

    if (response.PTZStatus == nullptr) {
        LogSync::error("[ONVIF] GetStatus failed: empty PTZStatus response");
        return std::nullopt;
    }

    return mapStatus(*response.PTZStatus);
}

std::optional<std::vector<OnvifPtzPresetTour>> OnvifPtzClient::getPresetTours(
    const std::string& ptzServiceUrl,
    const std::string& profileToken) const {
    if (ptzServiceUrl.empty() || profileToken.empty()) {
        return std::nullopt;
    }

    auto context = makeOnvifSoapContext(options_);

    _tptz__GetPresetTours request;
    _tptz__GetPresetToursResponse response;
    request.ProfileToken = profileToken;

    if (!applySecurity(context.get(), options_, "GetPresetTours")) {
        return std::nullopt;
    }

    const int result = soap_call___tptz__GetPresetTours(
        context.get(),
        ptzServiceUrl.c_str(),
        kGetPresetToursAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] GetPresetTours failed: ", onvifSoapFaultString(context.get()));
        return std::nullopt;
    }

    std::vector<OnvifPtzPresetTour> tours;
    tours.reserve(response.PresetTour.size());
    for (const tt__PresetTour* tour : response.PresetTour) {
        if (tour != nullptr) {
            tours.push_back(mapPresetTour(*tour));
        }
    }
    return tours;
}

std::optional<OnvifPtzPresetTour> OnvifPtzClient::getPresetTour(
    const std::string& ptzServiceUrl,
    const std::string& profileToken,
    const std::string& presetTourToken) const {
    if (ptzServiceUrl.empty() || profileToken.empty() || presetTourToken.empty()) {
        return std::nullopt;
    }

    auto context = makeOnvifSoapContext(options_);

    _tptz__GetPresetTour request;
    _tptz__GetPresetTourResponse response;
    request.ProfileToken = profileToken;
    request.PresetTourToken = presetTourToken;

    if (!applySecurity(context.get(), options_, "GetPresetTour")) {
        return std::nullopt;
    }

    const int result = soap_call___tptz__GetPresetTour(
        context.get(),
        ptzServiceUrl.c_str(),
        kGetPresetTourAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] GetPresetTour failed: ", onvifSoapFaultString(context.get()));
        return std::nullopt;
    }
    if (response.PresetTour == nullptr) {
        LogSync::error("[ONVIF] GetPresetTour failed: empty PresetTour response");
        return std::nullopt;
    }

    return mapPresetTour(*response.PresetTour);
}

std::optional<OnvifPtzPresetTourOptions> OnvifPtzClient::getPresetTourOptions(
    const std::string& ptzServiceUrl,
    const std::string& profileToken,
    const std::string& presetTourToken) const {
    if (ptzServiceUrl.empty() || profileToken.empty()) {
        return std::nullopt;
    }

    auto context = makeOnvifSoapContext(options_);

    _tptz__GetPresetTourOptions request;
    _tptz__GetPresetTourOptionsResponse response;
    request.ProfileToken = profileToken;

    std::string tokenValue = presetTourToken;
    if (!tokenValue.empty()) {
        request.PresetTourToken = &tokenValue;
    }

    if (!applySecurity(context.get(), options_, "GetPresetTourOptions")) {
        return std::nullopt;
    }

    const int result = soap_call___tptz__GetPresetTourOptions(
        context.get(),
        ptzServiceUrl.c_str(),
        kGetPresetTourOptionsAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] GetPresetTourOptions failed: ", onvifSoapFaultString(context.get()));
        return std::nullopt;
    }
    if (response.Options == nullptr) {
        LogSync::error("[ONVIF] GetPresetTourOptions failed: empty Options response");
        return std::nullopt;
    }

    return mapPresetTourOptions(*response.Options);
}

std::optional<std::string> OnvifPtzClient::createPresetTour(
    const std::string& ptzServiceUrl,
    const std::string& profileToken) const {
    if (ptzServiceUrl.empty() || profileToken.empty()) {
        return std::nullopt;
    }

    auto context = makeOnvifSoapContext(options_);

    _tptz__CreatePresetTour request;
    _tptz__CreatePresetTourResponse response;
    request.ProfileToken = profileToken;

    if (!applySecurity(context.get(), options_, "CreatePresetTour")) {
        return std::nullopt;
    }

    const int result = soap_call___tptz__CreatePresetTour(
        context.get(),
        ptzServiceUrl.c_str(),
        kCreatePresetTourAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] CreatePresetTour failed: ", onvifSoapFaultString(context.get()));
        return std::nullopt;
    }

    return response.PresetTourToken;
}

OnvifPtzOperationStatus OnvifPtzClient::modifyPresetTour(
    const std::string& ptzServiceUrl,
    const std::string& profileToken,
    const OnvifPtzPresetTour& tour) const {
    if (ptzServiceUrl.empty() || profileToken.empty() || tour.token.empty()) {
        return OnvifPtzOperationStatus::InvalidArgument;
    }

    auto context = makeOnvifSoapContext(options_);

    tt__PresetTour* presetTour = makePresetTour(context.get(), tour);
    if (presetTour == nullptr) {
        LogSync::error("[ONVIF] ModifyPresetTour failed: invalid preset tour");
        return OnvifPtzOperationStatus::InvalidArgument;
    }

    _tptz__ModifyPresetTour request;
    _tptz__ModifyPresetTourResponse response;
    request.ProfileToken = profileToken;
    request.PresetTour = presetTour;

    if (!applySecurity(context.get(), options_, "ModifyPresetTour")) {
        return OnvifPtzOperationStatus::UpstreamError;
    }

    const int result = soap_call___tptz__ModifyPresetTour(
        context.get(),
        ptzServiceUrl.c_str(),
        kModifyPresetTourAction,
        &request,
        response);

    if (result != SOAP_OK) {
        return operationStatusFromSoapFault(context.get(), "ModifyPresetTour");
    }

    return OnvifPtzOperationStatus::Success;
}

OnvifPtzOperationStatus OnvifPtzClient::operatePresetTour(
    const std::string& ptzServiceUrl,
    const std::string& profileToken,
    const std::string& presetTourToken,
    OnvifPtzPresetTourOperation operation) const {
    if (ptzServiceUrl.empty() || profileToken.empty() || presetTourToken.empty()) {
        return OnvifPtzOperationStatus::InvalidArgument;
    }

    auto context = makeOnvifSoapContext(options_);

    _tptz__OperatePresetTour request;
    _tptz__OperatePresetTourResponse response;
    request.ProfileToken = profileToken;
    request.PresetTourToken = presetTourToken;
    request.Operation = presetTourOperationValue(operation);

    if (!applySecurity(context.get(), options_, "OperatePresetTour")) {
        return OnvifPtzOperationStatus::UpstreamError;
    }

    const int result = soap_call___tptz__OperatePresetTour(
        context.get(),
        ptzServiceUrl.c_str(),
        kOperatePresetTourAction,
        &request,
        response);

    if (result != SOAP_OK) {
        return operationStatusFromSoapFault(context.get(), "OperatePresetTour");
    }

    return OnvifPtzOperationStatus::Success;
}

OnvifPtzOperationStatus OnvifPtzClient::removePresetTour(
    const std::string& ptzServiceUrl,
    const std::string& profileToken,
    const std::string& presetTourToken) const {
    if (ptzServiceUrl.empty() || profileToken.empty() || presetTourToken.empty()) {
        return OnvifPtzOperationStatus::InvalidArgument;
    }

    auto context = makeOnvifSoapContext(options_);

    _tptz__RemovePresetTour request;
    _tptz__RemovePresetTourResponse response;
    request.ProfileToken = profileToken;
    request.PresetTourToken = presetTourToken;

    if (!applySecurity(context.get(), options_, "RemovePresetTour")) {
        return OnvifPtzOperationStatus::UpstreamError;
    }

    const int result = soap_call___tptz__RemovePresetTour(
        context.get(),
        ptzServiceUrl.c_str(),
        kRemovePresetTourAction,
        &request,
        response);

    if (result != SOAP_OK) {
        return operationStatusFromSoapFault(context.get(), "RemovePresetTour");
    }

    return OnvifPtzOperationStatus::Success;
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

bool OnvifPtzClient::absoluteMove(
    const std::string& ptzServiceUrl,
    const std::string& profileToken,
    float pan,
    float tilt,
    float zoom) const {
    if (ptzServiceUrl.empty() || profileToken.empty()) {
        return false;
    }

    auto context = makeOnvifSoapContext(options_);

    tt__PTZVector* position = makeVector(context.get(), pan, tilt, zoom, true);
    if (position == nullptr) {
        LogSync::error("[ONVIF] AbsoluteMove failed: unable to allocate position");
        return false;
    }

    _tptz__AbsoluteMove request;
    _tptz__AbsoluteMoveResponse response;
    request.ProfileToken = profileToken;
    request.Position = position;

    if (!applySecurity(context.get(), options_, "AbsoluteMove")) {
        return false;
    }

    const int result = soap_call___tptz__AbsoluteMove(
        context.get(),
        ptzServiceUrl.c_str(),
        kAbsoluteMoveAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] AbsoluteMove failed: ", onvifSoapFaultString(context.get()));
        return false;
    }

    return true;
}

bool OnvifPtzClient::relativeMove(
    const std::string& ptzServiceUrl,
    const std::string& profileToken,
    float pan,
    float tilt,
    float zoom) const {
    if (ptzServiceUrl.empty() || profileToken.empty()) {
        return false;
    }
    if (pan == 0.0f && tilt == 0.0f && zoom == 0.0f) {
        LogSync::error("[ONVIF] RelativeMove failed: empty translation");
        return false;
    }

    auto context = makeOnvifSoapContext(options_);

    tt__PTZVector* translation = makeVector(context.get(), pan, tilt, zoom, false);
    if (translation == nullptr) {
        LogSync::error("[ONVIF] RelativeMove failed: unable to allocate translation");
        return false;
    }

    _tptz__RelativeMove request;
    _tptz__RelativeMoveResponse response;
    request.ProfileToken = profileToken;
    request.Translation = translation;

    if (!applySecurity(context.get(), options_, "RelativeMove")) {
        return false;
    }

    const int result = soap_call___tptz__RelativeMove(
        context.get(),
        ptzServiceUrl.c_str(),
        kRelativeMoveAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] RelativeMove failed: ", onvifSoapFaultString(context.get()));
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
