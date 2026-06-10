#include "OnvifDeviceClient.h"

#include "LogSync.h"
#include "OnvifSoapContext.h"

// soapH.h는 lib/app-onvif/onvif_services.h를 입력으로 soapcpp2가 생성한 header다.
// 여기에는 Device Service 타입과 soap_call___tds__* 선언이 들어 있다.
#include "soapH.h"

namespace {

constexpr const char* kGetDeviceInformationAction = "http://www.onvif.org/ver10/device/wsdl/GetDeviceInformation";
constexpr const char* kGetCapabilitiesAction = "http://www.onvif.org/ver10/device/wsdl/GetCapabilities";

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

    auto context = makeOnvifSoapContext(options_);

    // _tds__* 타입과 soap_call___tds__* 함수는 devicemgmt.wsdl에서 생성된 gSOAP 산출물이다.
    _tds__GetDeviceInformation request;
    _tds__GetDeviceInformationResponse response;

    if (!applyOnvifWsSecurity(context.get(), options_.wsSecurity)) {
        return std::nullopt;
    }

    const int result = soap_call___tds__GetDeviceInformation(
        context.get(),
        deviceServiceUrl.c_str(),
        kGetDeviceInformationAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] GetDeviceInformation failed: ", onvifSoapFaultString(context.get()));
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

    auto context = makeOnvifSoapContext(options_);

    _tds__GetCapabilities request;
    _tds__GetCapabilitiesResponse response;

    // 전체 capability를 요청한다. 응답에서 Media XAddr를 얻어 다음 Media Service 호출로 이어간다.
    request.Category.push_back(tt__CapabilityCategory::All);

    if (!applyOnvifWsSecurity(context.get(), options_.wsSecurity)) {
        return std::nullopt;
    }

    const int result = soap_call___tds__GetCapabilities(
        context.get(),
        deviceServiceUrl.c_str(),
        kGetCapabilitiesAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] GetCapabilities failed: ", onvifSoapFaultString(context.get()));
        return std::nullopt;
    }

    if (response.Capabilities == nullptr) {
        LogSync::error("[ONVIF] GetCapabilities failed: empty Capabilities response");
        return std::nullopt;
    }

    return mapCapabilities(*response.Capabilities);
}
