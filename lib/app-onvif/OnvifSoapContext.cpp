#include "OnvifSoapContext.h"

#include <ctime>

#include "LogSync.h"
#include "soapH.h"
#include "wsseapi.h"

// namespaces[]는 생성된 *.nsmap 파일이 정의하는 전역 namespace table이다.
// 현재는 OnvifDiscoveryService.cpp가 wsdd.nsmap을 include해 한 번만 정의하고,
// SOAP client들은 같은 table을 extern으로 공유한다.
extern struct Namespace namespaces[];

std::unique_ptr<soap, void (*)(soap*)> makeOnvifSoapContext(const OnvifClientOptions& options) {
    soap* context = soap_new1(SOAP_IO_DEFAULT);
    soap_set_namespaces(context, namespaces);
    context->connect_timeout = static_cast<int>(options.connectTimeout.count());
    context->send_timeout = static_cast<int>(options.sendTimeout.count());
    context->recv_timeout = static_cast<int>(options.recvTimeout.count());

    if (options.wsSecurity.enabled) {
        soap_register_plugin(context, soap_wsse);
    }

    return {context, [](soap* item) {
        if (item == nullptr) {
            return;
        }
        soap_destroy(item);
        soap_end(item);
        soap_free(item);
    }};
}

const char* onvifSoapFaultString(soap* context) {
    const char** fault = soap_faultstring(context);
    return fault != nullptr && *fault != nullptr ? *fault : "unknown SOAP fault";
}

bool applyOnvifWsSecurity(soap* context, const OnvifWsSecurityOptions& options) {
    if (!options.enabled) {
        return true;
    }

    if (options.username.empty()) {
        LogSync::error("[ONVIF] WS-Security is enabled but username is empty");
        return false;
    }

    if (options.timestampLifetime.count() > 0) {
        if (soap_wsse_add_Timestamp(context, "Time", static_cast<time_t>(options.timestampLifetime.count())) != SOAP_OK) {
            LogSync::error("[ONVIF] WS-Security Timestamp failed: ", onvifSoapFaultString(context));
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
        LogSync::error("[ONVIF] WS-Security UsernameToken failed: ", onvifSoapFaultString(context));
        return false;
    }

    return true;
}
