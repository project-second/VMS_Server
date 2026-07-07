#ifndef ONVIF_SOAP_CONTEXT_H
#define ONVIF_SOAP_CONTEXT_H

#include "OnvifClientOptions.h"

#include <memory>

struct soap;

std::unique_ptr<soap, void (*)(soap*)> makeOnvifSoapContext(const OnvifClientOptions& options);
const char* onvifSoapFaultString(soap* context);
bool applyOnvifWsSecurity(soap* context, const OnvifWsSecurityOptions& options);

#endif // ONVIF_SOAP_CONTEXT_H
