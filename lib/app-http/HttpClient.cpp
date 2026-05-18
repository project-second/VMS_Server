#include "HttpClient.h"

#include <fcntl.h>
#include <poll.h>

bool is_unreserved(char c) {
    const unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '-' || c == '_' || c == '.' || c == '~';
}

std::string url_encode(const std::string& input) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(input.size() * 3);
    for (char c : input) {
        if (is_unreserved(c)) {
            out.push_back(c);
            continue;
        }
        const unsigned char uc = static_cast<unsigned char>(c);
        out.push_back('%');
        out.push_back(kHex[(uc >> 4) & 0x0F]);
        out.push_back(kHex[uc & 0x0F]);
    }
    return out;
}

std::string append_query_params(std::string path, const HttpClient::QueryParams& query_params) {
    if (query_params.empty()) return path;

    const bool has_query = path.find('?') != std::string::npos;
    path.push_back(has_query ? '&' : '?');

    bool first = true;
    for (const auto& [key, value] : query_params) {
        if (!first) path.push_back('&');
        first = false;

        path += url_encode(key);
        path.push_back('=');
        path += url_encode(value);
    }

    return path;
}

std::string to_lower_copy(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool parse_content_length_from_headers(const std::string& headers, size_t& out_len) {
    out_len = 0;
    std::string lower = to_lower_copy(headers);
    const std::string key = "\r\ncontent-length:";
    size_t pos = lower.find(key);
    if (pos == std::string::npos) return false;

    size_t value_pos = pos + key.size();
    while (value_pos < lower.size() &&
        std::isspace(static_cast<unsigned char>(lower[value_pos]))) {
        ++value_pos;
    }

    size_t end = value_pos;
    while (end < lower.size() && std::isdigit(static_cast<unsigned char>(lower[end]))) {
        ++end;
    }
    if (end == value_pos) return false;

    try {
        out_len = static_cast<size_t>(std::stoull(lower.substr(value_pos, end - value_pos)));
    } catch (...) {
        return false;
    }
    return true;
}

int parse_status_code_from_headers(const std::string& headers) {
    size_t line_end = headers.find("\r\n");
    std::string start_line = (line_end == std::string::npos) ? headers : headers.substr(0, line_end);

    size_t a = start_line.find(' ');
    if (a == std::string::npos) return 0;
    size_t b = start_line.find(' ', a + 1);

    std::string status = (b == std::string::npos)
        ? start_line.substr(a + 1)
        : start_line.substr(a + 1, b - (a + 1));

    try {
        return std::stoi(status);
    } catch (...) {
        return 0;
    }
}

bool has_chunked_encoding(const std::string& headers) {
    std::string lower = to_lower_copy(headers);
    return lower.find("\r\ntransfer-encoding:") != std::string::npos &&
        lower.find("chunked") != std::string::npos;
}

std::string strip_ipv6_brackets(const std::string& s) {
    if (s.size() >= 2 && s.front() == '[' && s.back() == ']') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

bool is_ipv6_literal(const std::string& host) {
    in6_addr addr6{};
    return inet_pton(AF_INET6, host.c_str(), &addr6) == 1;
}

std::string format_host_header(std::string host, int port) {
    host = strip_ipv6_brackets(host);
    if (is_ipv6_literal(host)) {
        return "[" + host + "]:" + std::to_string(port);
    }
    return host + ":" + std::to_string(port);
}

bool should_close_after_response(const Http::Response& response) {
    const std::string* connection = response.headers.get("connection");
    if (connection) {
        const std::string conn = to_lower_copy(*connection);
        if (conn.find("close") != std::string::npos) return true;
        if (conn.find("keep-alive") != std::string::npos) return false;
    }

    return to_lower_copy(response.version) == "http/1.0";
}

void clear_websocket_client_headers(Http::Headers& headers) {
    headers.kv.erase("upgrade");
    headers.kv.erase("connection");
    headers.kv.erase("sec-websocket-version");
    headers.kv.erase("sec-websocket-key");
    headers.kv.erase("sec-websocket-protocol");
}

bool send_all(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}


bool set_socket_timeout(int fd, int seconds) {
    timeval tv{};
    tv.tv_sec = seconds;
    tv.tv_usec = 0;

    if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return false;
    }

    if (::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        return false;
    }

    return true;
}

bool connect_with_timeout(int fd, const sockaddr* addr, socklen_t addrlen, int seconds) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return false;
    }

    const int rc = connect(fd, addr, addrlen);
    if (rc == 0) {
        (void) fcntl(fd, F_SETFL, flags);
        return true;
    }

    if (errno != EINPROGRESS) {
        (void) fcntl(fd, F_SETFL, flags);
        return false;
    }

    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLOUT;

    const int poll_rc = poll(&pfd, 1, seconds * 1000);
    if (poll_rc <= 0) {
        (void) fcntl(fd, F_SETFL, flags);
        return false;
    }

    int so_error = 0;
    socklen_t so_error_len = sizeof(so_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) < 0) {
        (void) fcntl(fd, F_SETFL, flags);
        return false;
    }

    (void) fcntl(fd, F_SETFL, flags);
    return so_error == 0;
}

void HttpClient::applyAuthorizationHeader() {
    if (auth_options.type == HttpAuthOptions::Type::Bearer) {
        setHeaders("Authorization", "Bearer " + auth_options.bearer_token);
        return;
    }
    removeHeader("Authorization");
}

bool HttpClient::loadDigestSessionForRequest(const std::string& request_uri, const std::string& method_name,
    HttpDigestSession& digest_session, bool& has_saved_session) {
    has_saved_session = false;

    if (auth_options.type != HttpAuthOptions::Type::Digest ||
        auth_options.session_provider == nullptr ||
        auth_options.session_key.empty()) {
        return false;
    }

    has_saved_session = auth_options.session_provider->loadDigestSession(auth_options.session_key, digest_session);
    if (!has_saved_session) {
        removeHeader("Authorization");
        return false;
    }

    DigestAuthContext auth_ctx;
    auth_ctx.username = auth_options.username;
    auth_ctx.password = auth_options.password;
    auth_ctx.method = method_name;
    auth_ctx.uri = request_uri;
    auth_ctx.nonce_count = digest_session.nonce_count++;
    setHeaders("Authorization", build_digest_authorization(digest_session.challenge, auth_ctx));
    auth_options.session_provider->saveDigestSession(auth_options.session_key, digest_session);
    return true;
}

void HttpClient::clearDigestSession() {
    if (auth_options.session_provider && !auth_options.session_key.empty()) {
        auth_options.session_provider->clearDigestSession(auth_options.session_key);
    }
}

bool HttpClient::handleDigestChallenge(const Http::Response& response, const std::string& request_uri,
    const std::string& method_name) {
    if (auth_options.type != HttpAuthOptions::Type::Digest ||
        response.status_code != static_cast<int>(HttpCode::Unauthorized)) {
        return false;
    }

    const std::string* p = response.headers.get("WWW-Authenticate");
    if (p == nullptr) {
        clearDigestSession();
        return false;
    }

    DigestChallenge challenge;
    try {
        challenge = parse_www_authenticate_digest(*p);
    } catch (...) {
        clearDigestSession();
        return false;
    }

    DigestAuthContext auth_ctx;
    auth_ctx.username = auth_options.username;
    auth_ctx.password = auth_options.password;
    auth_ctx.method = method_name;
    auth_ctx.uri = request_uri;
    auth_ctx.nonce_count = 1;
    setHeaders("Authorization", build_digest_authorization(challenge, auth_ctx));

    if (auth_options.session_provider && !auth_options.session_key.empty()) {
        auth_options.session_provider->saveDigestSession(auth_options.session_key, HttpDigestSession{challenge, 2});
    }

    return true;
}

bool HttpClient::connectSocket() {
    closeSocket();
    constexpr int kDefaultSocketTimeoutSec = 10;
    constexpr int kDefaultConnectTimeoutSec = 5;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;      // IPv4 + IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    const std::string service = std::to_string(port);
    addrinfo* results = nullptr;
    const int gai_rc = getaddrinfo(host.c_str(), service.c_str(), &hints, &results);
    if (gai_rc != 0) {
        throw std::invalid_argument(std::string("getaddrinfo error: ") + gai_strerror(gai_rc));
    }

    for (addrinfo* ai = results; ai != nullptr; ai = ai->ai_next) {
        const int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) {
            continue;
        }

        if (connect_with_timeout(fd, ai->ai_addr, ai->ai_addrlen, kDefaultConnectTimeoutSec)) {
            if (set_socket_timeout(fd, kDefaultSocketTimeoutSec) == false) {
                close(fd);
                continue;
            }
            sockfd = fd;
            break;
        }

        close(fd);
    }

    freeaddrinfo(results);

    return sockfd >= 0;
}
void HttpClient::closeSocket() {
    if (sockfd >= 0) {
        close(sockfd);
        sockfd = -1;
    }
}

bool HttpClient::ensureConnected() {
    if (sockfd >= 0) return true;
    return connectSocket();
}

HttpClient::HttpClient(std::string host_, int port_) 
    : host{host_}, port{port_} {
    host = strip_ipv6_brackets(host);
    if (!connectSocket()) {
        throw std::invalid_argument("connect error");
    }
}

HttpClient::~HttpClient() {
    closeSocket();
}

void HttpClient::setHeaders(std::string optionName, std::string optionValue) {
    headers.set(optionName, optionValue);
}

void HttpClient::removeHeader(const std::string& optionName) {
    headers.kv.erase(to_lower(optionName));
}

void HttpClient::setAuthOptions(HttpAuthOptions options) {
    auth_options = std::move(options);
}

void HttpClient::clientSend(HttpMethod method, std::string path
    , std::string content, const QueryParams& query_params) {
    if (!ensureConnected()) {
        return;
    }

    std::ostringstream oss;
    const std::string request_path = append_query_params(std::move(path), query_params);

    oss << getMethodStr(method) << " " << request_path << " HTTP/1.1\r\n";
    if (!headers.has("User-Agent")) {
        oss << "User-Agent: simple-c-client/1.0\r\n";
    }
    if (!headers.has("Connection")) {
        oss << "Connection: keep-alive\r\n";
    }
    if (!headers.has("Host")) {
        oss << "Host: " << format_host_header(host, port) << "\r\n";
    }

    for(const auto& [k, v] : headers.kv) {
        oss << k << ": " << v <<"\r\n";
    }
    oss << "Content-Length: " << content.length() << "\r\n";
    oss << "\r\n";
    oss << content;

    if (!send_all(sockfd, oss.str())) {
        closeSocket();
    }
}

bool HttpClient::clientRecive(Http::Response& response) {
    std::string raw;
    raw.reserve(8 * 1024);

    constexpr size_t kBufSize = 4096;
    char buf[kBufSize];

    size_t header_end = std::string::npos;
    size_t content_length = 0;
    bool content_length_known = false;
    bool chunked = false;

    for (;;) {
        ssize_t n = recv(sockfd, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recv");
            return false;
        }
        if (n == 0) {
            // peer closed: no more data
            closeSocket();
            break;
        }

        raw.append(buf, static_cast<size_t>(n));

        if (header_end == std::string::npos) {
            size_t p = raw.find("\r\n\r\n");
            if (p != std::string::npos) {
                header_end = p;
                std::string headers = raw.substr(0, header_end + 4);
                content_length_known = parse_content_length_from_headers(headers, content_length);
                chunked = has_chunked_encoding(headers);
                const int status_code = parse_status_code_from_headers(headers);
                if (!content_length_known && !chunked &&
                    ((status_code >= 100 && status_code < 200) || status_code == 204 || status_code == 304)) {
                    break;
                }
            }
        }

        if (header_end != std::string::npos) {
            const size_t body_start = header_end + 4;

            if (content_length_known) {
                if (raw.size() >= body_start + content_length) break;
            } else if (chunked) {
                // simple end marker for chunked body without trailers
                if (raw.find("\r\n0\r\n\r\n", body_start) != std::string::npos) break;
            }
        }
    }

    if (raw.empty()) return false;

    std::string err;

    if (!Http::parse_response(raw, response, err)) {
        std::cerr << "parse failed: " << err << "\n";
        return false;
    }

    if (should_close_after_response(response)) {
        closeSocket();
    }

    return true;
}

bool HttpClient::request(HttpMethod method, std::string path, std::string content, Http::Response& response
    , const QueryParams& query_params) {
    applyAuthorizationHeader();
    const std::string request_uri = append_query_params(path, query_params);
    const std::string method_name = getMethodStr(method);

    if (auth_options.type == HttpAuthOptions::Type::Digest) {
        HttpDigestSession digest_session;
        bool has_saved_session = false;
        loadDigestSessionForRequest(request_uri, method_name, digest_session, has_saved_session);
    }

    if (!ensureConnected()) {
        return false;
    }

    clientSend(method, path, content, query_params);
    if (sockfd < 0) {
        return false;
    }
    if (!clientRecive(response)) {
        return false;
    }

    if (auth_options.type != HttpAuthOptions::Type::Digest ||
        response.status_code != static_cast<int>(HttpCode::Unauthorized)) {
        return true;
    }

    if (!handleDigestChallenge(response, request_uri, method_name)) {
        return true;
    }

    if (!ensureConnected()) {
        return false;
    }

    clientSend(method, path, content, query_params);
    if (sockfd < 0) {
        return false;
    }
    if (!clientRecive(response)) {
        return false;
    }

    if (response.status_code == static_cast<int>(HttpCode::Unauthorized) &&
        auth_options.session_provider && !auth_options.session_key.empty()) {
        clearDigestSession();
    }

    return true;
}

std::unique_ptr<WebSocketClientConnection> HttpClient::upgradeToWebSocket(
    std::string path,
    const QueryParams& query_params,
    const std::string& subprotocol) {
    applyAuthorizationHeader();
    const std::string request_uri = append_query_params(path, query_params);
    const std::string method_name = getMethodStr(HttpMethod::GET);

    auto send_handshake = [&](const std::string& client_key, Http::Response& response) -> bool {
        if (!ensureConnected()) return false;

        clientSend(HttpMethod::GET, path, "", query_params);
        if (sockfd < 0) return false;
        return clientRecive(response);
    };

    std::string client_key;
    if (!websocket_prepare_client_headers(headers, client_key, subprotocol)) {
        return nullptr;
    }

    HttpDigestSession digest_session;
    bool has_saved_session = false;
    loadDigestSessionForRequest(request_uri, method_name, digest_session, has_saved_session);

    Http::Response response;
    if (!send_handshake(client_key, response)) {
        clear_websocket_client_headers(headers);
        return nullptr;
    }

    if (auth_options.type == HttpAuthOptions::Type::Digest &&
        response.status_code == static_cast<int>(HttpCode::Unauthorized)) {
        if (handleDigestChallenge(response, request_uri, method_name)) {
            closeSocket();
            if (!websocket_prepare_client_headers(headers, client_key, subprotocol)) {
                clear_websocket_client_headers(headers);
                return nullptr;
            }
            if (!send_handshake(client_key, response)) {
                clear_websocket_client_headers(headers);
                return nullptr;
            }
        }
    }

    clear_websocket_client_headers(headers);

    if (!websocket_validate_server_response(response, client_key, subprotocol)) {
        if (response.status_code == static_cast<int>(HttpCode::Unauthorized) &&
            auth_options.session_provider && !auth_options.session_key.empty()) {
            clearDigestSession();
        }
        closeSocket();
        return nullptr;
    }

    int ws_fd = sockfd;
    sockfd = -1;
    return std::make_unique<WebSocketClientConnection>(ws_fd);
}
