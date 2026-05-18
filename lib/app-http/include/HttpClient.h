#ifndef __HTTPCLIENT_H__
#define __HTTPCLIENT_H__

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdexcept>
#include <iostream>
#include <memory>
#include <vector>
#include <utility>

#include "Digest.h"
#include "HttpCodes.h"
#include "HttpParser.h"
#include "WebSocket.h"

struct HttpDigestSession {
    DigestChallenge challenge;
    std::uint32_t nonce_count = 1;
};

class HttpAuthSessionProvider {
public:
    virtual ~HttpAuthSessionProvider() = default;
    virtual bool loadDigestSession(const std::string& session_key, HttpDigestSession& out) = 0;
    virtual void saveDigestSession(const std::string& session_key, const HttpDigestSession& session) = 0;
    virtual void clearDigestSession(const std::string& session_key) = 0;
};

struct HttpAuthOptions {
    enum class Type {
        None,
        Digest,
        Bearer
    };

    Type type = Type::None;
    std::string session_key;
    std::string username;
    std::string password;
    std::string bearer_token;
    HttpAuthSessionProvider* session_provider = nullptr;
};

class HttpClient {
private:
    int sockfd = -1;
    std::string host;
    int port;
    Http::Headers headers;
    HttpAuthOptions auth_options;

private:
    bool connectSocket();
    void closeSocket();
    bool ensureConnected();
    void applyAuthorizationHeader();
    bool loadDigestSessionForRequest(const std::string& request_uri, const std::string& method_name,
        HttpDigestSession& digest_session, bool& has_saved_session);
    bool handleDigestChallenge(const Http::Response& response, const std::string& request_uri,
        const std::string& method_name);
    void clearDigestSession();

public:
    using QueryParams = std::vector<std::pair<std::string, std::string>>;

    /**
     * @brief Construct a new Http Client object
     * 
     * @param host_ 타겟 host IP 또는 주소
     * @param port_ 타겟 host 의 포트
     */
    HttpClient(std::string host_, int port_);
    ~HttpClient();

    /**
     * @brief HTTP 헤더 옵션 추가 설정
     * 
     * @param optionName HTTP 옵션 이름 예: Content-Type
     * @param optionValue HTTP 옵션 값 예: application/soap+xml; charset=utf-8
     */
    void setHeaders(std::string optionName, std::string optionValue);
    void removeHeader(const std::string& optionName);
    void setAuthOptions(HttpAuthOptions options);

    /**
     * @brief HTTP Client 패킷 송신
     * 
     * @param method HTTP 메소드
     * @param path  url 경로 예: /onvif/device_service
     * @param content http body 내용
     * @param query_params GET 요청 등에서 사용할 URL query 파라미터 목록
     */
    void clientSend(HttpMethod method, std::string path, std::string content,
        const QueryParams& query_params = {});

    /**
     * @brief HTTP Client 패킷 수신
     * 
     * @param response HTTP 응답
     * @return true 수신 성공
     * @return false 수신 실패
     */
    bool clientRecive(Http::Response& response);

    /**
     * @brief 인증 옵션을 반영해서 HTTP 요청/응답을 1회 수행한다.
     *        Digest 옵션인 경우 401 challenge를 자동 처리해 재시도한다.
     */
    bool request(HttpMethod method, std::string path, std::string content, Http::Response& response
        , const QueryParams& query_params = {});

    /**
     * @brief HTTP 연결을 WebSocket으로 업그레이드하고, 성공 시 WebSocket 클라이언트 연결을 반환한다.
     */
    std::unique_ptr<WebSocketClientConnection> upgradeToWebSocket(std::string path,
        const QueryParams& query_params = {}, const std::string& subprotocol = "");
};

std::string append_query_params(std::string path, const HttpClient::QueryParams& query_params);

#endif // __HTTPCLIENT_H__
