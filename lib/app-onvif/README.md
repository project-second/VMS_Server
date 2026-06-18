# app-onvif

`app-onvif`는 VMS 서버의 ONVIF 기능을 담당하는 모듈이다. SOAP XML을 직접 문자열로 만들지 않고, gSOAP가 생성한 타입과 client stub을 사용한다.

현재 구현 범위는 다음과 같다.

- WS-Discovery active Probe
- WS-Discovery passive listener: Hello, Bye 수신
- WS-Discovery Resolve
- ONVIF Device Service `GetDeviceInformation`
- ONVIF Device Service `GetCapabilities`
- ONVIF Media Service `GetProfiles`
- ONVIF Media Service `GetStreamUri`
- ONVIF PTZ Service `GetPresets`
- ONVIF PTZ Service `SetPreset`
- ONVIF PTZ Service `GotoPreset`
- ONVIF PTZ Service `RemovePreset`
- ONVIF PTZ Service `ContinuousMove`
- ONVIF PTZ Service `Stop`
- WS-Security UsernameToken 인증: PasswordDigest, PasswordText

아직 HTTP Digest 인증, PTZ preset tour, DB 저장, ONVIF 결과의 application flow 연결은 구현하지 않았다.

## Directory

```text
lib/app-onvif/
  CMakeLists.txt
  OnvifDiscoveryService.cpp
  OnvifDeviceClient.cpp
  OnvifMediaClient.cpp
  OnvifPtzClient.cpp
  OnvifSoapContext.cpp
  onvif_services.h
  include/
    OnvifClientOptions.h
    OnvifDiscoveryService.h
    OnvifDeviceClient.h
    OnvifMediaClient.h
    OnvifPtzClient.h
  wsdl/
    ver10/device/wsdl/devicemgmt.wsdl
    ver10/media/wsdl/media.wsdl
    ver10/schema/*.xsd
    ver20/ptz/wsdl/ptz.wsdl
  external/
    *.xsd
```

`wsdl/`과 `external/`은 gSOAP 코드 생성을 위한 입력 파일이다. 생성된 `soapH.h`, `soapC.cpp`, `soapClient.cpp`, `*.nsmap` 파일은 source tree가 아니라 CMake build directory 아래에 만들어진다.

## Build

상위 CMake에서 `add_subdirectory(lib/app-onvif)`로 포함된다. 이 모듈의 CMake는 빌드 중 다음 순서로 gSOAP 코드를 생성한다.

```text
devicemgmt.wsdl + media.wsdl + ptz.wsdl
  -> wsdl2h
  -> generated/onvif_services_wsdl.h
  -> soapcpp2(onvif_services.h)
  -> generated/soapH.h, soapC.cpp, soapClient.cpp, *.nsmap
```

필요한 도구와 library는 다음과 같다.

- `wsdl2h`
- `soapcpp2`
- `gsoapssl++`
- OpenSSL
- gSOAP plugin: `wsddapi.c`, `wsaapi.c`, `wsseapi.c`, `smdevp.c`, `mecevp.c`

Discovery, Device Service, Media Service, PTZ Service는 하나의 `onvif_services.h`로 통합 생성한다.

```cpp
#import "wsdd.h"
#import "wsse.h"
#import "onvif_services_wsdl.h"
```

이렇게 묶는 이유는 Discovery, Device, Media, PTZ를 따로 생성했을 때 `soapC.cpp` serializer symbol이 중복될 수 있기 때문이다.

## Discovery 사용법

가장 단순한 1회 Discover 예시는 다음과 같다.

```cpp
#include "OnvifDiscoveryService.h"

OnvifDiscoveryService discovery;
std::vector<OnvifDiscoveredDevice> devices = discovery.discoverOnce();

for (const OnvifDiscoveredDevice& device : devices) {
    // device.endpointReference: 장비 식별자에 가까운 값
    // device.xaddrs: Device Service 호출에 사용할 endpoint 후보
}
```

주기 Probe와 passive listener를 함께 켜려면 `start()`를 사용한다.

```cpp
OnvifDiscoveryOptions options;
options.interval = std::chrono::milliseconds(300000);

OnvifDiscoveryService discovery(options);
discovery.start();

// application running...

discovery.stop();
```

`start()`는 내부적으로 다음을 함께 수행한다.

- 설정된 interval마다 active Probe 실행
- UDP 3702 passive listener 실행
- Hello 수신 시 장비 online/upsert
- Bye 수신 시 장비 offline 처리

`start()`를 사용하면 `discoverOnce()`처럼 결과를 바로 반환하지 않는다. 대신 서비스 내부의 장비 snapshot이 갱신되므로, 외부에서는 `devices()`로 현재 상태를 읽어 후속 Device Service 호출에 넘긴다.

```cpp
OnvifDiscoveryOptions options;
options.interval = std::chrono::milliseconds(300000);

OnvifDiscoveryService discovery(options);
OnvifDeviceClient deviceClient;

discovery.start();

while (running) {
    std::vector<OnvifDiscoveredDevice> devices = discovery.devices();

    for (const OnvifDiscoveredDevice& device : devices) {
        if (!device.online || device.xaddrs.empty()) {
            continue;
        }

        const std::string& deviceServiceUrl = device.xaddrs.front();

        std::optional<OnvifDeviceInformation> info =
            deviceClient.getDeviceInformation(deviceServiceUrl);

        std::optional<OnvifDeviceCapabilities> capabilities =
            deviceClient.getCapabilities(deviceServiceUrl);

        // TODO: cache update, DB upsert, admin API 노출
    }

    std::this_thread::sleep_for(std::chrono::minutes(5));
}

discovery.stop();
```

주의할 점은 매번 모든 장비에 `GetDeviceInformation`, `GetCapabilities`를 호출하면 장비에 불필요한 요청이 반복될 수 있다는 것이다. 운영 구조에서는 새로 발견된 장비나 metadata가 바뀐 장비만 조회하는 중간 service를 두는 편이 좋다.

```cpp
class OnvifDeviceProvisioningService {
public:
    OnvifDeviceProvisioningService(
        OnvifDiscoveryService& discovery,
        OnvifDeviceClient& deviceClient)
        : discovery_(discovery),
          deviceClient_(deviceClient) {}

    void syncOnce() {
        std::vector<OnvifDiscoveredDevice> devices = discovery_.devices();

        for (const OnvifDiscoveredDevice& device : devices) {
            if (!device.online || device.xaddrs.empty()) {
                continue;
            }

            if (processedEndpointReferences_.find(device.endpointReference) !=
                processedEndpointReferences_.end()) {
                continue;
            }

            const std::string& deviceServiceUrl = device.xaddrs.front();

            std::optional<OnvifDeviceInformation> info =
                deviceClient_.getDeviceInformation(deviceServiceUrl);
            std::optional<OnvifDeviceCapabilities> capabilities =
                deviceClient_.getCapabilities(deviceServiceUrl);

            if (!info || !capabilities) {
                continue;
            }

            processedEndpointReferences_.insert(device.endpointReference);

            // TODO: DB upsert 또는 application cache update
        }
    }

private:
    OnvifDiscoveryService& discovery_;
    OnvifDeviceClient& deviceClient_;
    std::set<std::string> processedEndpointReferences_;
};
```

## Resolve 사용법

이미 알고 있는 EndpointReference의 `XAddrs`를 다시 확인하려면 `resolveOnce()`를 사용한다.

```cpp
std::optional<OnvifDiscoveredDevice> resolved =
    discovery.resolveOnce(endpointReference);

if (resolved && !resolved->xaddrs.empty()) {
    const std::string& deviceServiceUrl = resolved->xaddrs.front();
}
```

## GetDeviceInformation 사용법

`GetDeviceInformation`에는 Discovery 결과의 `xaddrs` 중 Device Service URL을 넘긴다. `endpointReference`가 아니라 `xaddrs`를 사용해야 한다.

```cpp
#include "OnvifDeviceClient.h"
#include "OnvifDiscoveryService.h"

OnvifDiscoveryService discovery;
OnvifDeviceClient deviceClient;

std::vector<OnvifDiscoveredDevice> devices = discovery.discoverOnce();

for (const OnvifDiscoveredDevice& device : devices) {
    if (device.xaddrs.empty()) {
        continue;
    }

    const std::string& deviceServiceUrl = device.xaddrs.front();
    std::optional<OnvifDeviceInformation> info =
        deviceClient.getDeviceInformation(deviceServiceUrl);

    if (!info) {
        continue;
    }

    // info->manufacturer
    // info->model
    // info->firmwareVersion
    // info->serialNumber
    // info->hardwareId

    std::optional<OnvifDeviceCapabilities> capabilities =
        deviceClient.getCapabilities(deviceServiceUrl);

    if (!capabilities) {
        continue;
    }

    // capabilities->mediaXAddr
    // capabilities->eventsXAddr
    // capabilities->imagingXAddr
    // capabilities->ptzXAddr
}
```

`GetCapabilities` 응답의 `mediaXAddr`는 이후 Media Service `GetProfiles`, `GetStreamUri` 호출 endpoint로 사용한다.

## GetProfiles 사용법

`GetProfiles`에는 `GetCapabilities`에서 얻은 `mediaXAddr`를 넘긴다. 응답의 `profile.token`은 `GetStreamUri` 요청의 `ProfileToken`으로 사용한다.

```cpp
#include "OnvifDeviceClient.h"
#include "OnvifDiscoveryService.h"
#include "OnvifMediaClient.h"

OnvifDiscoveryService discovery;
OnvifDeviceClient deviceClient;
OnvifMediaClient mediaClient;

std::vector<OnvifDiscoveredDevice> devices = discovery.discoverOnce();

for (const OnvifDiscoveredDevice& device : devices) {
    if (device.xaddrs.empty()) {
        continue;
    }

    std::optional<OnvifDeviceCapabilities> capabilities =
        deviceClient.getCapabilities(device.xaddrs.front());

    if (!capabilities || capabilities->mediaXAddr.empty()) {
        continue;
    }

    std::optional<std::vector<OnvifMediaProfile>> profiles =
        mediaClient.getProfiles(capabilities->mediaXAddr);

    if (!profiles) {
        continue;
    }

    for (const OnvifMediaProfile& profile : *profiles) {
        std::optional<OnvifStreamUri> streamUri =
            mediaClient.getStreamUri(capabilities->mediaXAddr, profile.token);

        if (!streamUri) {
            continue;
        }

        // profile.token
        // profile.name
        // streamUri->uri: media-server 수신 서버에 넘길 RTSP/RTP URL
    }
}
```

`getStreamUri()`의 기본값은 라이브 RTSP 수신에 흔히 쓰는 `RTP-Unicast over RTSP`다. 필요하면 stream type과 transport protocol을 명시할 수 있다.

```cpp
std::optional<OnvifStreamUri> streamUri = mediaClient.getStreamUri(
    mediaXAddr,
    profileToken,
    OnvifMediaStreamType::RtpUnicast,
    OnvifMediaTransportProtocol::Rtsp);
```

## PTZ 사용법

PTZ 호출에는 `GetCapabilities`에서 얻은 `ptzXAddr`와 Media Service `GetProfiles`에서 얻은 `profile.token`이 필요하다. `ptzXAddr`는 PTZ Service endpoint이고, `profile.token`은 어떤 Media Profile 기준으로 PTZ를 제어할지 지정하는 값이다.

```cpp
#include "OnvifDeviceClient.h"
#include "OnvifDiscoveryService.h"
#include "OnvifMediaClient.h"
#include "OnvifPtzClient.h"

OnvifDiscoveryService discovery;
OnvifDeviceClient deviceClient;
OnvifMediaClient mediaClient;
OnvifPtzClient ptzClient;

std::vector<OnvifDiscoveredDevice> devices = discovery.discoverOnce();

for (const OnvifDiscoveredDevice& device : devices) {
    if (device.xaddrs.empty()) {
        continue;
    }

    std::optional<OnvifDeviceCapabilities> capabilities =
        deviceClient.getCapabilities(device.xaddrs.front());

    if (!capabilities || capabilities->mediaXAddr.empty() || capabilities->ptzXAddr.empty()) {
        continue;
    }

    std::optional<std::vector<OnvifMediaProfile>> profiles =
        mediaClient.getProfiles(capabilities->mediaXAddr);

    if (!profiles || profiles->empty()) {
        continue;
    }

    const std::string& profileToken = profiles->front().token;
    const std::string& ptzServiceUrl = capabilities->ptzXAddr;

    // PTZ 호출은 ptzServiceUrl + profileToken 조합으로 수행한다.
}
```

프리셋 목록은 `getPresets()`로 조회한다.

```cpp
std::optional<std::vector<OnvifPtzPreset>> presets =
    ptzClient.getPresets(ptzServiceUrl, profileToken);

if (presets) {
    for (const OnvifPtzPreset& preset : *presets) {
        // preset.token
        // preset.name
    }
}
```

현재 위치를 프리셋으로 저장하려면 `setPreset()`을 사용한다. 새 프리셋을 만들 때는 preset token을 비워두고, 기존 프리셋을 갱신할 때는 기존 token을 함께 넘긴다.

```cpp
std::optional<std::string> presetToken =
    ptzClient.setPreset(ptzServiceUrl, profileToken, "Gate");

std::optional<std::string> updatedPresetToken =
    ptzClient.setPreset(ptzServiceUrl, profileToken, "Gate", "Preset001");
```

저장된 프리셋 위치로 이동하거나 삭제할 때는 preset token을 사용한다.

```cpp
bool moved = ptzClient.gotoPreset(ptzServiceUrl, profileToken, "Preset001");
bool removed = ptzClient.removePreset(ptzServiceUrl, profileToken, "Preset001");
```

연속 이동은 `continuousMove()`로 수행한다. `pan`, `tilt`, `zoom` 값은 장비의 PTZ velocity space에 맞는 정규화된 속도 값으로 넘긴다. 일반적으로 `-1.0`부터 `1.0` 범위를 먼저 시도한다.

```cpp
// 오른쪽으로 pan 이동
bool started = ptzClient.continuousMove(ptzServiceUrl, profileToken, 0.5f, 0.0f);

// 위로 tilt 이동
started = ptzClient.continuousMove(ptzServiceUrl, profileToken, 0.0f, 0.5f);

// 확대 zoom 이동
started = ptzClient.continuousMove(ptzServiceUrl, profileToken, 0.0f, 0.0f, 0.5f);
```

연속 이동을 멈출 때는 `stop()`을 호출한다.

```cpp
bool stopped = ptzClient.stop(ptzServiceUrl, profileToken);

// pan/tilt만 멈추고 zoom은 유지
stopped = ptzClient.stop(ptzServiceUrl, profileToken, true, false);
```

PTZ는 장비별 지원 범위 차이가 크다. 실제 운영에서는 `GetNodes`, `GetConfigurationOptions` 같은 capability/options 계열 응답으로 지원 space와 속도 범위를 확인한 뒤 UI 입력 범위를 제한하는 편이 좋다. 현재 `OnvifPtzClient`는 기본 이동, 정지, 프리셋 기능만 감싸며 preset tour는 아직 구현하지 않았다.

WS-Security가 필요한 장비라면 Device client, Media client, PTZ client에 같은 옵션을 넘기면 된다.

```cpp
OnvifClientOptions options;
options.wsSecurity.enabled = true;
options.wsSecurity.username = "admin";
options.wsSecurity.password = "password";

OnvifDeviceClient deviceClient(options);
OnvifMediaClient mediaClient(options);
OnvifPtzClient ptzClient(options);
```

실전에서는 `xaddrs.front()`를 바로 쓰기보다 접근 가능한 HTTP/HTTPS endpoint를 고르는 helper를 두는 편이 좋다.

```cpp
std::optional<std::string> pickDeviceServiceUrl(const OnvifDiscoveredDevice& device) {
    for (const std::string& xaddr : device.xaddrs) {
        if (xaddr.rfind("http://", 0) == 0 || xaddr.rfind("https://", 0) == 0) {
            return xaddr;
        }
    }

    return std::nullopt;
}
```

## WS-Security UsernameToken 사용법

인증이 필요한 장비는 `OnvifDeviceClientOptions`에 WS-Security UsernameToken 정보를 넣어 client를 생성한다. 기본 password type은 ONVIF에서 흔히 쓰는 `PasswordDigest`다.

```cpp
OnvifDeviceClientOptions options;
options.wsSecurity.enabled = true;
options.wsSecurity.username = "admin";
options.wsSecurity.password = "password";
options.wsSecurity.passwordType = OnvifWsSecurityPasswordType::Digest;
options.wsSecurity.timestampLifetime = std::chrono::seconds(300);

OnvifDeviceClient deviceClient(options);

std::optional<OnvifDeviceInformation> info =
    deviceClient.getDeviceInformation(deviceServiceUrl);
std::optional<OnvifDeviceCapabilities> capabilities =
    deviceClient.getCapabilities(deviceServiceUrl);
```

일부 장비가 `PasswordText`만 받는 경우에는 password type을 바꿀 수 있다. 단, `PasswordText`는 HTTPS 같은 보호된 전송과 함께 쓰는 것이 좋다.

```cpp
options.wsSecurity.passwordType = OnvifWsSecurityPasswordType::Text;
```

현재 WS-Security UsernameToken은 Device Service와 Media Service 호출에 적용된다. HTTP Digest 인증은 아직 구현하지 않았다.

## main.cpp 연결 방향

현재 `src/main.cpp`는 `OnvifDiscoveryService`를 생성하고 `start()`만 호출한다. `OnvifDeviceClient`와 `OnvifMediaClient`는 아직 main 실행 흐름에 직접 연결되어 있지 않다.

임시 테스트라면 `server.start()` 이후에 `discoverOnce()`를 한 번 호출하고, 반환된 `xaddrs`로 `getDeviceInformation()`을 호출할 수 있다. 다만 이 방식은 startup 시점에 blocking network 작업을 수행하므로 운영 코드로 오래 두기에는 적합하지 않다.

권장 방향은 Discovery와 DeviceClient를 묶는 별도 service를 추가하는 것이다.

```text
OnvifDiscoveryService
  -> OnvifDiscoveredDevice.xaddrs 확보
  -> OnvifDeviceClient.getDeviceInformation(xaddr)
  -> OnvifDeviceClient.getCapabilities(xaddr)
  -> capabilities.mediaXAddr
  -> OnvifMediaClient.getProfiles(mediaXAddr)
  -> OnvifMediaClient.getStreamUri(mediaXAddr, profileToken)
  -> DB 저장 또는 admin API 노출
```

## Network Test Notes

ONVIF Discovery는 UDP multicast `239.255.255.250:3702`를 사용한다. 따라서 테스트 환경의 네트워크 계층이 multicast 송수신을 허용해야 한다.

Linux Docker에서 실제 LAN 장비를 찾을 때는 일반 bridge network보다 host network가 단순하다.

```bash
docker run --rm -it --network host vms-image
```

Windows/WSL/Docker 계층에서는 multicast가 실제 LAN까지 전달되지 않거나 응답이 container까지 돌아오지 않을 수 있다. 이 경우 먼저 Windows host 도구나 WSL mirrored networking에서 직접 Discovery가 되는지 확인하는 편이 좋다.

## Current Limitations

- HTTP Digest 인증 미구현
- Discovery 결과 DB 저장 미구현
- `GetDeviceInformation`, `GetCapabilities` 결과의 application flow 연결 미구현
- `GetProfiles` 결과의 application flow 연결 미구현
- `GetStreamUri` 결과의 application flow 연결 미구현
- 실제 카메라 대상 상호 운용 테스트 필요
