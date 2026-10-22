/**
 * @file OnvifSoapContext.cpp
 * @brief ONVIF SOAP 컨텍스트의 공통 설정, 패킷 로깅 콜백 및 인증 헤더 구성 구현.
 */
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

/**
 * @brief 하나의 gSOAP 컨텍스트에 연결할 패킷 로그와 로거 연결 확인용 파일 경로.
 */
struct SoapLogFiles {
    std::string sent; ///< 송신 패킷 로그 경로.
    std::string recv; ///< 수신 패킷 로그 경로.
    std::string test; ///< 로거 연결 확인 메시지를 기록할 파일 경로.
};

/**
 * @brief 공유 로깅 설정과 컨텍스트별 로그 경로 접근을 보호하는 뮤텍스를 반환한다.
 * @return 프로세스 내에서 공유하는 로깅 상태 보호용 뮤텍스.
 */
std::mutex& soapLogMutex() {
    static std::mutex mutex;
    return mutex;
}

/**
 * @brief 이후 로거 연결에 적용할 공통 SOAP 로깅 설정을 반환한다.
 * @pre 설정에 접근하는 동안 soapLogMutex()를 잠가야 한다.
 * @return 공유 로깅 설정에 대한 참조.
 */
OnvifSoapLogOptions& soapLogOptions() {
    static OnvifSoapLogOptions options;
    return options;
}

/**
 * @brief gSOAP 컨텍스트와 로그 파일 경로를 연결하는 레지스트리를 반환한다.
 * @pre 레지스트리에 접근하는 동안 soapLogMutex()를 잠가야 한다.
 * @return 컨텍스트별 로그 경로를 저장하는 공유 맵.
 */
std::unordered_map<soap*, SoapLogFiles>& soapLogFilesByContext() {
    static std::unordered_map<soap*, SoapLogFiles> files;
    return files;
}

/**
 * @brief 로깅 콜백에서 실제 송신을 위임할 원래 gSOAP 송신 콜백의 저장소를 반환한다.
 * @return 공유 송신 함수 포인터에 대한 참조. 콜백 저장 전에는 nullptr이다.
 */
int (*&defaultSoapSend())(soap*, const char*, size_t) {
    static int (*fn)(soap*, const char*, size_t) = nullptr;
    return fn;
}

/**
 * @brief 로깅 콜백에서 실제 수신을 위임할 원래 gSOAP 수신 콜백의 저장소를 반환한다.
 * @return 공유 수신 함수 포인터에 대한 참조. 콜백 저장 전에는 nullptr이다.
 */
size_t (*&defaultSoapRecv())(soap*, char*, size_t) {
    static size_t (*fn)(soap*, char*, size_t) = nullptr;
    return fn;
}

/**
 * @brief 빈 디렉터리를 /tmp로 보정하고 루트 경로를 제외한 후행 슬래시를 제거한다.
 * @param directory 보정할 로그 출력 디렉터리 문자열.
 * @return 기본값과 후행 슬래시 처리를 적용한 디렉터리 문자열.
 */
std::string normalizeDirectory(std::string directory) {
    if (directory.empty()) {
        return "/tmp";
    }
    while (directory.size() > 1 && directory.back() == '/') {
        directory.pop_back();
    }
    return directory;
}

/**
 * @brief 로그 파일 끝에 지정한 바이트와 개행 문자를 추가한다.
 *
 * 경로가 비어 있거나 데이터가 nullptr 또는 0바이트이면 건너뛰고,
 * 파일 열기에 실패해도 호출자에게 별도 오류를 반환하지 않는다.
 * @param path 데이터를 추가할 파일 경로.
 * @param data 기록할 바이트 배열.
 * @param size 기록할 바이트 수.
 */
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

/**
 * @brief 컨텍스트에 등록된 송신 또는 수신 로그 경로를 조회한다.
 * @param context 로그 경로를 조회할 gSOAP 컨텍스트.
 * @param sent true이면 송신 경로, false이면 수신 경로를 선택한다.
 * @return 선택한 경로의 복사본. 등록되지 않은 컨텍스트이면 빈 문자열.
 */
std::string logPathFor(soap* context, bool sent) {
    std::lock_guard<std::mutex> lock(soapLogMutex());
    const auto it = soapLogFilesByContext().find(context);
    if (it == soapLogFilesByContext().end()) {
        return {};
    }
    return sent ? it->second.sent : it->second.recv;
}

/**
 * @brief 원래 송신 콜백을 호출하고 SOAP_OK로 완료되면 송신 버퍼를 기록한다.
 * @param context 송신에 사용할 gSOAP 컨텍스트.
 * @param data 송신할 바이트 배열.
 * @param size 송신할 바이트 수.
 * @return 원래 송신 콜백의 결과. 원래 콜백이 없으면 SOAP_EOF.
 */
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

/**
 * @brief 원래 수신 콜백을 호출하고 버퍼 범위 안에서 수신된 바이트를 기록한다.
 * @param context 수신에 사용할 gSOAP 컨텍스트.
 * @param data 수신한 데이터를 저장할 버퍼.
 * @param size 수신 버퍼의 최대 바이트 수.
 * @return 원래 수신 콜백이 반환한 바이트 수. 원래 콜백이 없으면 0.
 */
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

/**
 * @brief 디렉터리 경로를 보정한 뒤 공유 로깅 설정을 교체한다.
 * @param options 이후 컨텍스트에 로거를 연결할 때 사용할 설정.
 */
void configureOnvifSoapLogging(OnvifSoapLogOptions options) {
    std::lock_guard<std::mutex> lock(soapLogMutex());
    options.directory = normalizeDirectory(std::move(options.directory));
    soapLogOptions() = std::move(options);

    if (soapLogOptions().enabled) {
        LogSync::info("[ONVIF] SOAP packet logging enabled: ", soapLogOptions().directory);
    }
}

/**
 * @brief 현재 설정으로 로그 파일 경로를 등록하고 컨텍스트의 송수신 콜백을 교체한다.
 * @param context 로거를 연결할 컨텍스트. nullptr이거나 로깅이 비활성화되어 있으면 건너뛴다.
 * @param category 로그 파일 이름 구분자. nullptr이면 soap을 사용한다.
 */
void applyOnvifSoapLogging(soap* context, const char* category) {
    if (context == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(soapLogMutex());
    if (!soapLogOptions().enabled) {
        return;
    }

    // 로깅 래퍼 자신을 원래 콜백으로 저장해 재귀 호출하는 일을 피한다.
    if (context->fsend != onvifLoggedSend) {
        defaultSoapSend() = context->fsend;
    }
    if (context->frecv != onvifLoggedRecv) {
        defaultSoapRecv() = context->frecv;
    }

    // 출력 디렉터리와 카테고리가 같으면 로그 파일 경로를 공유한다.
    const std::string prefix = soapLogOptions().directory + "/onvif-" + (category == nullptr ? "soap" : category);
    SoapLogFiles files{
        .sent = prefix + "-sent.log",
        .recv = prefix + "-recv.log",
        .test = prefix + "-test.log",
    };

    // 패킷 통신 전에도 로거 연결 여부를 확인할 수 있도록 메시지를 남긴다.
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

/**
 * @brief 컨텍스트의 로그 경로 등록을 지우고 연결된 송수신 콜백은 유지한다.
 * @param context 등록을 해제할 컨텍스트. nullptr이면 건너뛴다.
 */
void removeOnvifSoapLogging(soap* context) {
    if (context == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(soapLogMutex());
    soapLogFilesByContext().erase(context);
}

/**
 * @brief ONVIF 클라이언트 설정을 적용하고 자동 정리되는 gSOAP 컨텍스트를 생성한다.
 * @param options 연결·송수신 타임아웃과 WS-Security 설정.
 * @return 로그 경로 등록 해제와 gSOAP 자원 해제를 수행하는 소유 포인터.
 */
std::unique_ptr<soap, void (*)(soap*)> makeOnvifSoapContext(const OnvifClientOptions& options) {
    // 생성된 ONVIF 네임스페이스와 클라이언트용 로거, 통신 제한 시간을 연결한다.
    soap* context = soap_new1(SOAP_IO_DEFAULT);
    soap_set_namespaces(context, namespaces);
    applyOnvifSoapLogging(context, "client");
    context->connect_timeout = static_cast<int>(options.connectTimeout.count());
    context->send_timeout = static_cast<int>(options.sendTimeout.count());
    context->recv_timeout = static_cast<int>(options.recvTimeout.count());

    // 플러그인은 생성 시 등록하고, 요청별 인증 헤더는 applyOnvifWsSecurity()에서 추가한다.
    if (options.wsSecurity.enabled) {
        soap_register_plugin(context, soap_wsse);
    }

    return {context, [](soap* item) {
        if (item == nullptr) {
            return;
        }
        // 로그 경로 등록을 먼저 제거한 뒤 객체, 할당 메모리, 컨텍스트 순으로 정리한다.
        removeOnvifSoapLogging(item);
        soap_destroy(item);
        soap_end(item);
        soap_free(item);
    }};
}

/**
 * @brief SOAP fault 설명을 반환하고, 설명이 없으면 공통 대체 문자열을 사용한다.
 * @param context 오류를 조회할 유효한 gSOAP 컨텍스트.
 * @return 소유권 이전 없이 반환하는 fault 문자열 또는 "unknown SOAP fault".
 */
const char* onvifSoapFaultString(soap* context) {
    const char** fault = soap_faultstring(context);
    return fault != nullptr && *fault != nullptr ? *fault : "unknown SOAP fault";
}

/**
 * @brief 활성화된 WS-Security 설정을 검증하고 요청용 Timestamp와 UsernameToken을 추가한다.
 * @param context WS-Security 플러그인이 등록된 유효한 gSOAP 컨텍스트.
 * @param options 인증 활성화 여부, 자격 증명 및 토큰 형식·유효기간 설정.
 * @return 비활성화 상태이거나 헤더 생성에 성공하면 true, 설정 또는 헤더 생성 오류 시 false.
 */
bool applyOnvifWsSecurity(soap* context, const OnvifWsSecurityOptions& options) {
    if (!options.enabled) {
        return true;
    }

    if (options.username.empty()) {
        LogSync::error("[ONVIF] WS-Security is enabled but username is empty");
        return false;
    }

    // 유효기간이 양수인 경우에만 Timestamp 헤더를 추가한다.
    if (options.timestampLifetime.count() > 0) {
        if (soap_wsse_add_Timestamp(context, "Time", static_cast<time_t>(options.timestampLifetime.count())) != SOAP_OK) {
            LogSync::error("[ONVIF] WS-Security Timestamp failed: ", onvifSoapFaultString(context));
            return false;
        }
    }

    // 빈 비밀번호는 nullptr로 전달하고, 설정에 따라 Text 또는 Digest 토큰을 생성한다.
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
