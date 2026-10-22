/**
 * @file OnvifSoapContext.h
 * @brief ONVIF SOAP 컨텍스트 생성, 패킷 로깅 및 WS-Security 적용을 위한 공통 인터페이스.
 */
#ifndef ONVIF_SOAP_CONTEXT_H
#define ONVIF_SOAP_CONTEXT_H

#include "OnvifClientOptions.h"

#include <memory>
#include <string>

struct soap;

/**
 * @brief ONVIF SOAP 송수신 패킷 로그의 활성화 여부와 출력 디렉터리를 지정한다.
 */
struct OnvifSoapLogOptions {
    bool enabled = false; ///< SOAP 패킷 로그 활성화 여부. 기본값은 비활성화.
    std::string directory = "/tmp"; ///< 로그 출력 디렉터리. 빈 값은 /tmp로 보정한다.
};

/**
 * @brief SOAP 로거 연결에 사용할 프로세스 공통 설정을 저장한다.
 *
 * 디렉터리 경로 끝의 불필요한 슬래시를 제거한다. 이미 로거가 연결된 컨텍스트의
 * 콜백과 로그 경로는 유지되며, 변경된 설정은 이후 applyOnvifSoapLogging() 호출에 사용된다.
 * @param options 로깅 활성화 여부와 출력 디렉터리 설정.
 */
void configureOnvifSoapLogging(OnvifSoapLogOptions options);

/**
 * @brief 로깅이 활성화되어 있으면 컨텍스트에 패킷 로깅용 송수신 콜백을 연결한다.
 *
 * 출력 디렉터리에 `onvif-<category>-sent.log`, `onvif-<category>-recv.log` 및
 * 로거 연결 확인용 `onvif-<category>-test.log`를 사용한다. 디렉터리는 미리 존재해야 한다.
 * @param context 로거를 연결할 gSOAP 컨텍스트. nullptr이면 작업을 건너뛴다.
 * @param category 로그 파일 이름에 사용할 구분자. nullptr이면 soap을 사용한다.
 */
void applyOnvifSoapLogging(soap* context, const char* category);

/**
 * @brief 컨텍스트에 등록된 로그 경로 정보를 제거한다.
 *
 * 컨텍스트 해제 전에 호출하며, 송수신 콜백은 유지한다.
 * @param context 로그 경로 등록을 해제할 컨텍스트. nullptr이면 작업을 건너뛴다.
 */
void removeOnvifSoapLogging(soap* context);

/**
 * @brief 공통 네임스페이스, 로깅, 타임아웃 및 WS-Security 플러그인을 설정한 컨텍스트를 생성한다.
 *
 * WS-Security가 활성화되어 있으면 플러그인을 등록한다. 인증 헤더는 각 요청 전에
 * applyOnvifWsSecurity()로 추가한다.
 * @param options 연결·송수신 타임아웃과 WS-Security 설정.
 * @return 소멸 시 로그 경로 등록과 gSOAP 자원을 정리하는 소유 포인터.
 */
std::unique_ptr<soap, void (*)(soap*)> makeOnvifSoapContext(const OnvifClientOptions& options);

/**
 * @brief gSOAP 컨텍스트에서 SOAP 오류 설명을 조회한다.
 * @param context 오류를 조회할 유효한 gSOAP 컨텍스트.
 * @return SOAP fault 문자열. 설명이 없으면 "unknown SOAP fault". 문자열의 소유권은 이전되지 않는다.
 */
const char* onvifSoapFaultString(soap* context);

/**
 * @brief WS-Security 설정에 따라 요청에 Timestamp와 UsernameToken 헤더를 추가한다.
 *
 * timestampLifetime이 양수일 때만 Timestamp를 추가하고, passwordType에 따라
 * PasswordText 또는 PasswordDigest 방식의 UsernameToken을 구성한다.
 * @pre WS-Security가 활성화되어 있으면 context에 soap_wsse 플러그인이 등록되어 있어야 한다.
 * @param context 인증 헤더를 추가할 유효한 gSOAP 컨텍스트.
 * @param options 인증 활성화 여부, 사용자 이름, 비밀번호 및 토큰 설정.
 * @return 비활성화 상태이거나 헤더 추가에 성공하면 true, 사용자 이름 누락 또는 헤더 추가 실패 시 false.
 */
bool applyOnvifWsSecurity(soap* context, const OnvifWsSecurityOptions& options);

#endif // ONVIF_SOAP_CONTEXT_H
