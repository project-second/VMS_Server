#include "OnvifDeviceClient.h"

#include <memory>

#include "LogSync.h"

// soapH.h는 lib/app-onvif/onvif_services.h를 입력으로 soapcpp2가 생성한 header다.
// 여기에는 Device Service 타입과 soap_call___tds__GetDeviceInformation 선언이 들어 있다.
#include "soapH.h"

// namespaces[]는 생성된 *.nsmap 파일이 정의하는 전역 namespace table이다.
// 현재는 OnvifDiscoveryService.cpp가 wsdd.nsmap을 include해 한 번만 정의하고,
// Device client는 같은 table을 extern으로 공유한다.
extern struct Namespace namespaces[];

namespace {

constexpr const char* kGetDeviceInformationAction = "http://www.onvif.org/ver10/device/wsdl/GetDeviceInformation";

std::unique_ptr<soap, void (*)(soap*)> makeSoapContext(const OnvifDeviceClientOptions& options) {
    // Device Service는 HTTP 기반 SOAP 호출이므로 기본 I/O context를 사용한다.
    soap* context = soap_new1(SOAP_IO_DEFAULT);
    soap_set_namespaces(context, namespaces);
    context->connect_timeout = static_cast<int>(options.connectTimeout.count());
    context->send_timeout = static_cast<int>(options.sendTimeout.count());
    context->recv_timeout = static_cast<int>(options.recvTimeout.count());

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

    const int result = soap_call___tds__GetDeviceInformation(
        context.get(),
        deviceServiceUrl.c_str(),
        kGetDeviceInformationAction,
        &request,
        response);

    if (result != SOAP_OK) {
        LogSync::error("[ONVIF] GetDeviceInformation failed: ", *soap_faultstring(context.get()));
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
