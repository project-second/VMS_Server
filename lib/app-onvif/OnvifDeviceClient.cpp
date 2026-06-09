#include "OnvifDeviceClient.h"

#include <ctime>
#include <memory>

#include "LogSync.h"

// soapH.h는 lib/app-onvif/onvif_services.h를 입력으로 soapcpp2가 생성한 header다.
// 여기에는 Device Service 타입과 soap_call___tds__* 선언이 들어 있다.
#include "soapH.h"
#include "wsseapi.h"

// namespaces[]는 생성된 *.nsmap 파일이 정의하는 전역 namespace table이다.
// 현재는 OnvifDiscoveryService.cpp가 wsdd.nsmap을 include해 한 번만 정의하고,
// Device client는 같은 table을 extern으로 공유한다.
extern struct Namespace namespaces[];

namespace {

constexpr const char* kGetDeviceInformationAction = "http://www.onvif.org/ver10/device/wsdl/GetDeviceInformation";
constexpr const char* kGetCapabilitiesAction = "http://www.onvif.org/ver10/device/wsdl/GetCapabilities";

std::unique_ptr<soap, void (*)(soap*)> makeSoapContext(const OnvifDeviceClientOptions& options) {
    // Device Service는 HTTP 기반 SOAP 호출이므로 기본 I/O context를 사용한다.
    soap* context = soap_new1(SOAP_IO_DEFAULT);
    soap_set_namespaces(context, namespaces);
    context->connect_timeout = static_cast<int>(options.connectTimeout.count());
    context->send_timeout = static_cast<int>(options.sendTimeout.count());
    context->recv_timeout = static_cast<int>(options.recvTimeout.count());

    if (options.wsSecurity.enabled) {
        soap_register_plugin(context, soap_wsse);
    }

    // gSOAP context는 C API 객체라서 destroy/end/free 순서로 정리해야 한다.
    return {context, [](soap* item) {
        if (item == nullptr) {
            return;
        }
        soap_destroy(item);
        soap_end(item);
        soap_free(item);
    }};
}

const char* soapFaultString(soap* context) {
    const char** fault = soap_faultstring(context);
    return fault != nullptr && *fault != nullptr ? *fault : "unknown SOAP fault";
}

bool applyWsSecurity(soap* context, const OnvifWsSecurityOptions& options) {
    if (!options.enabled) {
        return true;
    }

    if (options.username.empty()) {
        LogSync::error("[ONVIF] WS-Security is enabled but username is empty");
        return false;
    }

    if (options.timestampLifetime.count() > 0) {
        if (soap_wsse_add_Timestamp(context, "Time", static_cast<time_t>(options.timestampLifetime.count())) != SOAP_OK) {
            LogSync::error("[ONVIF] WS-Security Timestamp failed: ", soapFaultString(context));
            return false;
        }
    }

    const char* password = options.password.empty() ? nullptr : options.password.c_str();
    int result = SOAP_OK;
    if (options.passwordType == OnvifWsSecurityPasswordType::Text) {
        result = soap_wsse_add_UsernameTokenText(context, "UsernameToken", options.username.c_str(), password);
    } else {
        result = soap_wsse_add_UsernameTokenDigest(context, "UsernameToken", options.username.c_str(), password);
    }

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] WS-Security UsernameToken failed: ", soapFaultString(context));
        return false;
    }

    return true;
}

OnvifDeviceCapabilities mapCapabilities(const tt__Capabilities& capabilities) {
    OnvifDeviceCapabilities result;

    if (capabilities.Analytics != nullptr) {
        result.analyticsXAddr = capabilities.Analytics->XAddr;
    }
    if (capabilities.Device != nullptr) {
        result.deviceXAddr = capabilities.Device->XAddr;
    }
    if (capabilities.Events != nullptr) {
        result.eventsXAddr = capabilities.Events->XAddr;
    }
    if (capabilities.Imaging != nullptr) {
        result.imagingXAddr = capabilities.Imaging->XAddr;
    }
    if (capabilities.Media != nullptr) {
        result.mediaXAddr = capabilities.Media->XAddr;
    }
    if (capabilities.PTZ != nullptr) {
        result.ptzXAddr = capabilities.PTZ->XAddr;
    }

    return result;
}

} // namespace

OnvifDeviceClient::OnvifDeviceClient(OnvifDeviceClientOptions options)
    : options_(options) {}

std::optional<OnvifDeviceInformation> OnvifDeviceClient::getDeviceInformation(
    const std::string& deviceServiceUrl) const {
    if (deviceServiceUrl.empty()) {
        return std::nullopt;
    }

    auto context = makeSoapContext(options_);

    // _tds__* 타입과 soap_call___tds__* 함수는 devicemgmt.wsdl에서 생성된 gSOAP 산출물이다.
    _tds__GetDeviceInformation request;
    _tds__GetDeviceInformationResponse response;

    if (!applyWsSecurity(context.get(), options_.wsSecurity)) {
        return std::nullopt;
    }

    const int result = soap_call___tds__GetDeviceInformation(
        context.get(),
        deviceServiceUrl.c_str(),
        kGetDeviceInformationAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] GetDeviceInformation failed: ", soapFaultString(context.get()));
        return std::nullopt;
    }

    OnvifDeviceInformation info;
    info.manufacturer = response.Manufacturer;
    info.model = response.Model;
    info.firmwareVersion = response.FirmwareVersion;
    info.serialNumber = response.SerialNumber;
    info.hardwareId = response.HardwareId;
    return info;
}

std::optional<OnvifDeviceCapabilities> OnvifDeviceClient::getCapabilities(
    const std::string& deviceServiceUrl) const {
    if (deviceServiceUrl.empty()) {
        return std::nullopt;
    }

    auto context = makeSoapContext(options_);

    _tds__GetCapabilities request;
    _tds__GetCapabilitiesResponse response;

    // 전체 capability를 요청한다. 응답에서 Media XAddr를 얻어 다음 Media Service 호출로 이어간다.
    request.Category.push_back(tt__CapabilityCategory::All);

    if (!applyWsSecurity(context.get(), options_.wsSecurity)) {
        return std::nullopt;
    }

    const int result = soap_call___tds__GetCapabilities(
        context.get(),
        deviceServiceUrl.c_str(),
        kGetCapabilitiesAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] GetCapabilities failed: ", soapFaultString(context.get()));
        return std::nullopt;
    }

    if (response.Capabilities == nullptr) {
        LogSync::error("[ONVIF] GetCapabilities failed: empty Capabilities response");
        return std::nullopt;
    }

    return mapCapabilities(*response.Capabilities);
}
