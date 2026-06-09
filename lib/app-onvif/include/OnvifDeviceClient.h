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

// Device Service SOAP 호출에 적용할 네트워크 timeout 설정이다.
struct OnvifDeviceClientOptions {
    std::chrono::seconds connectTimeout{3};
    std::chrono::seconds sendTimeout{5};
    std::chrono::seconds recvTimeout{5};
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
    // 현재는 인증을 적용하지 않으므로 인증이 필요한 장비는 이후 HTTP Digest 또는
    // WS-Security UsernameToken 처리가 추가되어야 한다.
    std::optional<OnvifDeviceInformation> getDeviceInformation(const std::string& deviceServiceUrl) const;

private:
    OnvifDeviceClientOptions options_;
};

#endif // ONVIF_DEVICE_CLIENT_H
