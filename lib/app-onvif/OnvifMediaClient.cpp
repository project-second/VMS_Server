#include "OnvifMediaClient.h"

#include "LogSync.h"
#include "OnvifSoapContext.h"

// soapH.h는 lib/app-onvif/onvif_services.h를 입력으로 soapcpp2가 생성한 header다.
// 여기에는 Media Service 타입과 soap_call___trt__* 선언이 들어 있다.
#include "soapH.h"

namespace {

constexpr const char* kGetProfilesAction = "http://www.onvif.org/ver10/media/wsdl/GetProfiles";
constexpr const char* kGetStreamUriAction = "http://www.onvif.org/ver10/media/wsdl/GetStreamUri";


tt__StreamType toSoapStreamType(OnvifMediaStreamType value) {
    switch (value) {
    case OnvifMediaStreamType::RtpMulticast:
        return tt__StreamType::RTP_Multicast;
    case OnvifMediaStreamType::RtpUnicast:
    default:
        return tt__StreamType::RTP_Unicast;
    }
}

tt__TransportProtocol toSoapTransportProtocol(OnvifMediaTransportProtocol value) {
    switch (value) {
    case OnvifMediaTransportProtocol::Udp:
        return tt__TransportProtocol::UDP;
    case OnvifMediaTransportProtocol::Tcp:
        return tt__TransportProtocol::TCP;
    case OnvifMediaTransportProtocol::Http:
        return tt__TransportProtocol::HTTP;
    case OnvifMediaTransportProtocol::Rtsp:
    default:
        return tt__TransportProtocol::RTSP;
    }
}

OnvifStreamUri mapStreamUri(const tt__MediaUri& mediaUri) {
    OnvifStreamUri result;
    result.uri = mediaUri.Uri;
    result.invalidAfterConnect = mediaUri.InvalidAfterConnect;
    result.invalidAfterReboot = mediaUri.InvalidAfterReboot;
    result.timeout = mediaUri.Timeout;
    return result;
}

std::string tokenOf(const tt__ConfigurationEntity* entity) {
    return entity != nullptr ? entity->token : std::string{};
}

OnvifMediaProfile mapProfile(const tt__Profile& profile) {
    OnvifMediaProfile result;
    result.token = profile.token;
    result.name = profile.Name;
    result.fixed = profile.fixed != nullptr ? *profile.fixed : false;
    result.videoSourceConfigurationToken = tokenOf(profile.VideoSourceConfiguration);
    result.videoEncoderConfigurationToken = tokenOf(profile.VideoEncoderConfiguration);
    result.audioSourceConfigurationToken = tokenOf(profile.AudioSourceConfiguration);
    result.audioEncoderConfigurationToken = tokenOf(profile.AudioEncoderConfiguration);
    return result;
}

} // namespace

OnvifMediaClient::OnvifMediaClient(OnvifMediaClientOptions options)
    : options_(options) {}

std::optional<std::vector<OnvifMediaProfile>> OnvifMediaClient::getProfiles(
    const std::string& mediaServiceUrl) const {
    if (mediaServiceUrl.empty()) {
        return std::nullopt;
    }

    auto context = makeOnvifSoapContext(options_);

    _trt__GetProfiles request;
    _trt__GetProfilesResponse response;

    if (!applyOnvifWsSecurity(context.get(), options_.wsSecurity)) {
        return std::nullopt;
    }

    const int result = soap_call___trt__GetProfiles(
        context.get(),
        mediaServiceUrl.c_str(),
        kGetProfilesAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] GetProfiles failed: ", onvifSoapFaultString(context.get()));
        return std::nullopt;
    }

    std::vector<OnvifMediaProfile> profiles;
    profiles.reserve(response.Profiles.size());
    for (const tt__Profile* profile : response.Profiles) {
        if (profile == nullptr) {
            continue;
        }
        profiles.push_back(mapProfile(*profile));
    }

    return profiles;
}


std::optional<OnvifStreamUri> OnvifMediaClient::getStreamUri(
    const std::string& mediaServiceUrl,
    const std::string& profileToken,
    OnvifMediaStreamType streamType,
    OnvifMediaTransportProtocol transportProtocol) const {
    if (mediaServiceUrl.empty() || profileToken.empty()) {
        return std::nullopt;
    }

    auto context = makeOnvifSoapContext(options_);

    tt__Transport* transport = soap_new_tt__Transport(context.get());
    tt__StreamSetup* streamSetup = soap_new_tt__StreamSetup(context.get());
    if (transport == nullptr || streamSetup == nullptr) {
        LogSync::error("[ONVIF] GetStreamUri failed: unable to allocate request objects");
        return std::nullopt;
    }

    transport->soap_default(context.get());
    transport->Protocol = toSoapTransportProtocol(transportProtocol);
    transport->Tunnel = nullptr;

    streamSetup->soap_default(context.get());
    streamSetup->Stream = toSoapStreamType(streamType);
    streamSetup->Transport = transport;

    _trt__GetStreamUri request;
    _trt__GetStreamUriResponse response;
    request.StreamSetup = streamSetup;
    request.ProfileToken = profileToken;

    if (!applyOnvifWsSecurity(context.get(), options_.wsSecurity)) {
        return std::nullopt;
    }

    const int result = soap_call___trt__GetStreamUri(
        context.get(),
        mediaServiceUrl.c_str(),
        kGetStreamUriAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] GetStreamUri failed: ", onvifSoapFaultString(context.get()));
        return std::nullopt;
    }

    if (response.MediaUri == nullptr || response.MediaUri->Uri.empty()) {
        LogSync::error("[ONVIF] GetStreamUri failed: empty MediaUri response");
        return std::nullopt;
    }

    return mapStreamUri(*response.MediaUri);
}
