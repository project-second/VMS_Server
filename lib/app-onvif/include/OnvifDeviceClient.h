#ifndef ONVIF_DEVICE_CLIENT_H
#define ONVIF_DEVICE_CLIENT_H

#include <chrono>
#include <optional>
#include <string>

// ONVIF Device Service의 GetDeviceInformation 응답을 애플리케이션에서 쓰기 쉬운
// 형태로 옮긴 DTO다.
struct OnvifDeviceInformation {
    std::string manufacturer;
    std::string model;
    std::string firmwareVersion;
    std::string serialNumber;
    std::string hardwareId;
};

// ONVIF Device Service의 GetCapabilities 응답 중 이후 service 호출에 필요한
// endpoint URL만 추린 DTO다.
struct OnvifDeviceCapabilities {
    std::string analyticsXAddr;
    std::string deviceXAddr;
    std::string eventsXAddr;
    std::string imagingXAddr;
    std::string mediaXAddr;
    std::string ptzXAddr;
};

enum class OnvifWsSecurityPasswordType {
    Text,
    Digest,
};

// ONVIF Device/Media SOAP 호출에 붙일 WS-Security UsernameToken 설정이다.
// 기본은 비활성화이며, ONVIF 장비는 보통 PasswordDigest 방식을 요구한다.
struct OnvifWsSecurityOptions {
    bool enabled = false;
    std::string username;
    std::string password;
    OnvifWsSecurityPasswordType passwordType = OnvifWsSecurityPasswordType::Digest;
    std::chrono::seconds timestampLifetime{300};
};

// Device Service SOAP 호출에 적용할 네트워크 timeout 및 인증 설정이다.
struct OnvifDeviceClientOptions {
    std::chrono::seconds connectTimeout{3};
    std::chrono::seconds sendTimeout{5};
    std::chrono::seconds recvTimeout{5};
    OnvifWsSecurityOptions wsSecurity;
};

// ONVIF Device Management Service를 호출하는 얇은 client wrapper다.
//
// 실제 SOAP 직렬화/역직렬화와 HTTP 호출 함수는 gSOAP가 생성한 코드를 사용한다.
// 이 class는 애플리케이션 코드가 생성 타입에 직접 의존하지 않도록 endpoint URL과
// 내부 DTO 사이의 변환 경계를 제공한다.
class OnvifDeviceClient {
public:
    explicit OnvifDeviceClient(OnvifDeviceClientOptions options = {});

    // Discovery의 XAddrs에서 얻은 Device Service URL로 GetDeviceInformation을 호출한다.
    // URL이 비어 있거나 SOAP fault/transport 오류가 발생하면 std::nullopt를 반환한다.
    // options.wsSecurity가 활성화되어 있으면 WS-Security UsernameToken을 함께 전송한다.
    std::optional<OnvifDeviceInformation> getDeviceInformation(const std::string& deviceServiceUrl) const;

    // Discovery의 XAddrs에서 얻은 Device Service URL로 GetCapabilities를 호출한다.
    // 응답의 Device/Media/Event/PTZ 등 service endpoint XAddr를 추려 반환한다.
    // Media Service로 넘어갈 때는 보통 mediaXAddr를 다음 SOAP endpoint로 사용한다.
    std::optional<OnvifDeviceCapabilities> getCapabilities(const std::string& deviceServiceUrl) const;

private:
    OnvifDeviceClientOptions options_;
};

#endif // ONVIF_DEVICE_CLIENT_H
