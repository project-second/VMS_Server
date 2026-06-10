#ifndef ONVIF_MEDIA_CLIENT_H
#define ONVIF_MEDIA_CLIENT_H

#include "OnvifClientOptions.h"

#include <optional>
#include <string>
#include <vector>

using OnvifMediaClientOptions = OnvifClientOptions;

// ONVIF Media Service의 GetProfiles 응답 중 스트림 URI 조회에 필요한
// profile token과 주요 media configuration token을 추린 DTO다.
struct OnvifMediaProfile {
    std::string token;
    std::string name;
    bool fixed = false;
    std::string videoSourceConfigurationToken;
    std::string videoEncoderConfigurationToken;
    std::string audioSourceConfigurationToken;
    std::string audioEncoderConfigurationToken;
};

// ONVIF Media Service를 호출하는 얇은 client wrapper다.
//
// Device Service의 GetCapabilities에서 얻은 mediaXAddr를 endpoint로 받아
// gSOAP가 생성한 Media Service client stub을 호출한다.
class OnvifMediaClient {
public:
    explicit OnvifMediaClient(OnvifMediaClientOptions options = {});

    // mediaXAddr로 GetProfiles를 호출한다.
    // 응답된 profile token은 이후 GetStreamUri 요청의 ProfileToken으로 사용한다.
    std::optional<std::vector<OnvifMediaProfile>> getProfiles(const std::string& mediaServiceUrl) const;

private:
    OnvifMediaClientOptions options_;
};

#endif // ONVIF_MEDIA_CLIENT_H
