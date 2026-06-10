#ifndef ONVIF_CLIENT_OPTIONS_H
#define ONVIF_CLIENT_OPTIONS_H

#include <chrono>
#include <string>

enum class OnvifWsSecurityPasswordType {
    Text,
    Digest,
};

// ONVIF SOAP 호출에 붙일 WS-Security UsernameToken 설정이다.
// 기본은 비활성화이며, ONVIF 장비는 보통 PasswordDigest 방식을 요구한다.
struct OnvifWsSecurityOptions {
    bool enabled = false;
    std::string username;
    std::string password;
    OnvifWsSecurityPasswordType passwordType = OnvifWsSecurityPasswordType::Digest;
    std::chrono::seconds timestampLifetime{300};
};

// Device/Media Service SOAP 호출에 적용할 네트워크 timeout 및 인증 설정이다.
struct OnvifClientOptions {
    std::chrono::seconds connectTimeout{3};
    std::chrono::seconds sendTimeout{5};
    std::chrono::seconds recvTimeout{5};
    OnvifWsSecurityOptions wsSecurity;
};

#endif // ONVIF_CLIENT_OPTIONS_H
