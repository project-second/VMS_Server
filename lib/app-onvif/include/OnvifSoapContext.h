#ifndef ONVIF_SOAP_CONTEXT_H
#define ONVIF_SOAP_CONTEXT_H

#include "OnvifClientOptions.h"

#include <memory>
#include <string>

struct soap;

struct OnvifSoapLogOptions {
    bool enabled = false;
    std::string directory = "/tmp";
};

void configureOnvifSoapLogging(OnvifSoapLogOptions options);
void applyOnvifSoapLogging(soap* context, const char* category);
void removeOnvifSoapLogging(soap* context);
std::unique_ptr<soap, void (*)(soap*)> makeOnvifSoapContext(const OnvifClientOptions& options);
const char* onvifSoapFaultString(soap* context);
bool applyOnvifWsSecurity(soap* context, const OnvifWsSecurityOptions& options);

#endif // ONVIF_SOAP_CONTEXT_H
