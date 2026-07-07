#include "OnvifSoapContext.h"

#include <ctime>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include "LogSync.h"
#include "soapH.h"
#include "wsseapi.h"

// namespaces[]는 생성된 *.nsmap 파일이 정의하는 전역 namespace table이다.
// 현재는 OnvifDiscoveryService.cpp가 wsdd.nsmap을 include해 한 번만 정의하고,
// SOAP client들은 같은 table을 extern으로 공유한다.
extern struct Namespace namespaces[];

namespace {

struct SoapLogFiles {
    std::string sent;
    std::string recv;
    std::string test;
};

std::mutex& soapLogMutex() {
    static std::mutex mutex;
    return mutex;
}

OnvifSoapLogOptions& soapLogOptions() {
    static OnvifSoapLogOptions options;
    return options;
}

std::unordered_map<soap*, SoapLogFiles>& soapLogFilesByContext() {
    static std::unordered_map<soap*, SoapLogFiles> files;
    return files;
}

int (*&defaultSoapSend())(soap*, const char*, size_t) {
    static int (*fn)(soap*, const char*, size_t) = nullptr;
    return fn;
}

size_t (*&defaultSoapRecv())(soap*, char*, size_t) {
    static size_t (*fn)(soap*, char*, size_t) = nullptr;
    return fn;
}

std::string normalizeDirectory(std::string directory) {
    if (directory.empty()) {
        return "/tmp";
    }
    while (directory.size() > 1 && directory.back() == '/') {
        directory.pop_back();
    }
    return directory;
}

void appendLogFile(const std::string& path, const char* data, std::size_t size) {
    if (path.empty() || data == nullptr || size == 0) {
        return;
    }

    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) {
        return;
    }
    output.write(data, static_cast<std::streamsize>(size));
    output.put('\n');
}

std::string logPathFor(soap* context, bool sent) {
    std::lock_guard<std::mutex> lock(soapLogMutex());
    const auto it = soapLogFilesByContext().find(context);
    if (it == soapLogFilesByContext().end()) {
        return {};
    }
    return sent ? it->second.sent : it->second.recv;
}

int onvifLoggedSend(soap* context, const char* data, size_t size) {
    auto send = defaultSoapSend();
    if (send == nullptr) {
        return SOAP_EOF;
    }

    const int result = send(context, data, size);
    if (result == SOAP_OK) {
        appendLogFile(logPathFor(context, true), data, size);
    }
    return result;
}

size_t onvifLoggedRecv(soap* context, char* data, size_t size) {
    auto recv = defaultSoapRecv();
    if (recv == nullptr) {
        return 0;
    }

    const size_t result = recv(context, data, size);
    if (result > 0 && result <= size) {
        appendLogFile(logPathFor(context, false), data, result);
    }
    return result;
}

} // namespace

void configureOnvifSoapLogging(OnvifSoapLogOptions options) {
    std::lock_guard<std::mutex> lock(soapLogMutex());
    options.directory = normalizeDirectory(std::move(options.directory));
    soapLogOptions() = std::move(options);

    if (soapLogOptions().enabled) {
        LogSync::info("[ONVIF] SOAP packet logging enabled: ", soapLogOptions().directory);
    }
}

void applyOnvifSoapLogging(soap* context, const char* category) {
    if (context == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(soapLogMutex());
    if (!soapLogOptions().enabled) {
        return;
    }

    if (context->fsend != onvifLoggedSend) {
        defaultSoapSend() = context->fsend;
    }
    if (context->frecv != onvifLoggedRecv) {
        defaultSoapRecv() = context->frecv;
    }

    const std::string prefix = soapLogOptions().directory + "/onvif-" + (category == nullptr ? "soap" : category);
    SoapLogFiles files{
        .sent = prefix + "-sent.log",
        .recv = prefix + "-recv.log",
        .test = prefix + "-test.log",
    };

    {
        std::ofstream output(files.test, std::ios::app);
        if (output) {
            output << "attached gSOAP packet logger\n";
        }
    }

    soapLogFilesByContext()[context] = std::move(files);
    context->fsend = onvifLoggedSend;
    context->frecv = onvifLoggedRecv;
}

void removeOnvifSoapLogging(soap* context) {
    if (context == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(soapLogMutex());
    soapLogFilesByContext().erase(context);
}

std::unique_ptr<soap, void (*)(soap*)> makeOnvifSoapContext(const OnvifClientOptions& options) {
    soap* context = soap_new1(SOAP_IO_DEFAULT);
    soap_set_namespaces(context, namespaces);
    applyOnvifSoapLogging(context, "client");
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
        removeOnvifSoapLogging(item);
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
