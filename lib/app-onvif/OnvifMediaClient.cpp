#include "OnvifMediaClient.h"

#include "LogSync.h"
#include "OnvifSoapContext.h"

// soapH.h는 lib/app-onvif/onvif_services.h를 입력으로 soapcpp2가 생성한 header다.
// 여기에는 Media Service 타입과 soap_call___trt__* 선언이 들어 있다.
#include "soapH.h"

namespace {

constexpr const char* kGetProfilesAction = "http://www.onvif.org/ver10/media/wsdl/GetProfiles";

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
