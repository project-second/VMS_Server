#include "HttpServer.h"

#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "LogSync.h"

namespace {

bool parse_content_length_from_headers(const std::string& headers, size_t& out_len) {
    out_len = 0;
    std::string lower = to_lower(headers);
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

bool has_chunked_encoding(const std::string& headers) {
    std::string lower = to_lower(headers);
    return lower.find("\r\ntransfer-encoding:") != std::string::npos &&
        lower.find("chunked") != std::string::npos;
}

bool contains_token_ci(const std::string& csv, const std::string& token) {
    size_t start = 0;
    while (start <= csv.size()) {
        size_t comma = csv.find(',', start);
        std::string part = (comma == std::string::npos)
            ? csv.substr(start)
            : csv.substr(start, comma - start);
        trim_inplace(part);
        if (to_lower(part) == to_lower(token)) return true;
        if (comma == std::string::npos) break;
        start = comma + 1;
    }

    return false;
}

bool should_keep_alive_request(const Http::Request& req) {
    const std::string version = to_lower(req.version);
    const std::string* connection = req.headers.get("connection");

    if (version == "http/1.1") {
        if (connection && contains_token_ci(*connection, "close")) return false;
        return true;
    }

    if (version == "http/1.0") {
        return connection && contains_token_ci(*connection, "keep-alive");
    }

    return false;
}

std::string default_reason_phrase(int code) {
    return getCodeReason(static_cast<HttpCode>(code));
}

std::vector<std::string> split_path_segments(const std::string& path) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < path.size()) {
        while (start < path.size() && path[start] == '/') ++start;
        if (start >= path.size()) break;
        std::size_t end = start;
        while (end < path.size() && path[end] != '/') ++end;
        out.emplace_back(path.substr(start, end - start));
        start = end;
    }
    return out;
}

bool match_route_pattern(const std::string& pattern,
    const std::string& path,
    std::unordered_map<std::string, std::string>& out_params) {
    out_params.clear();

    const auto pattern_segments = split_path_segments(pattern);
    const auto path_segments = split_path_segments(path);
    if (pattern_segments.size() != path_segments.size()) return false;

    for (std::size_t i = 0; i < pattern_segments.size(); ++i) {
        const std::string& pseg = pattern_segments[i];
        const std::string& aseg = path_segments[i];

        if (pseg.size() >= 3 && pseg.front() == '{' && pseg.back() == '}') {
            const std::string key = pseg.substr(1, pseg.size() - 2);
            if (key.empty()) return false;
            out_params[key] = aseg;
            continue;
        }

        if (pseg != aseg) return false;
    }

    return true;
}

} // namespace

HttpServer::HttpServer(std::string bind_ip, int port, std::size_t worker_count)
    : bind_ip_(std::move(bind_ip))
    , port_(port)
    , worker_pool_(worker_count == 0 ? 1 : worker_count) {}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::setMiddleware(Middleware middleware) {
    std::lock_guard<std::mutex> lock(routes_mutex_);
    middleware_ = std::move(middleware);
}

bool HttpServer::addRoute(HttpMethod method, std::string path, Handler handler) {
    if (!handler) return false;

    const std::string key = routeKey(method, normalizePath(std::move(path)));

    std::lock_guard<std::mutex> lock(routes_mutex_);
    routes_[key] = std::move(handler);

    return true;
}

bool HttpServer::removeRoute(HttpMethod method, std::string path) {
    const std::string key = routeKey(method, normalizePath(std::move(path)));

    std::lock_guard<std::mutex> lock(routes_mutex_);
    return routes_.erase(key) > 0;
}

bool HttpServer::addWebSocketRoute(
    std::string path,
    WebSocketHandler handler,
    WebSocketHandshakeHandler handshake_handler) {
    if (!handler) return false;
    const std::string key = normalizePath(std::move(path));

    std::lock_guard<std::mutex> lock(routes_mutex_);
    ws_routes_[key] = std::move(handler);
    if (handshake_handler) {
        ws_handshake_routes_[key] = std::move(handshake_handler);
    } else {
        ws_handshake_routes_.erase(key);
    }
    return true;
}

bool HttpServer::removeWebSocketRoute(std::string path) {
    const std::string key = normalizePath(std::move(path));

    std::lock_guard<std::mutex> lock(routes_mutex_);
    ws_handshake_routes_.erase(key);
    return ws_routes_.erase(key) > 0;
}

bool HttpServer::start() {
    if (running_) return true;

    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        LogSync::error("[HttpServer] socket() failed: ", std::strerror(errno));
        return false;
    }

    if (!setNonBlocking(listen_fd_)) {
        LogSync::error("[HttpServer] setNonBlocking(listen) failed: ", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    int reuse = 1;
    if (::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        LogSync::error("[HttpServer] setsockopt(SO_REUSEADDR) failed: ", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind_ip_.empty() || bind_ip_ == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, bind_ip_.c_str(), &addr.sin_addr) != 1) {
        LogSync::error("[HttpServer] invalid bind ip: ", bind_ip_);
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LogSync::error("[HttpServer] bind() failed: ", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (::listen(listen_fd_, 256) < 0) {
        LogSync::error("[HttpServer] listen() failed: ", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    epoll_fd_ = ::epoll_create1(0);
    if (epoll_fd_ < 0) {
        LogSync::error("[HttpServer] epoll_create1() failed: ", std::strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd_;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev) < 0) {
        LogSync::error("[HttpServer] epoll_ctl(ADD listen) failed: ", std::strerror(errno));
        ::close(epoll_fd_);
        ::close(listen_fd_);
        epoll_fd_ = -1;
        listen_fd_ = -1;
        return false;
    }

    running_ = true;
    io_thread_ = std::thread([this]() { eventLoop(); });

    return true;
}

void HttpServer::stop() {
    if (!running_) return;

    running_ = false;

    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }

    if (io_thread_.joinable()) {
        io_thread_.join();
    }

    for (auto& [fd, _] : clients_) {
        (void)_;
        ::close(fd);
    }
    clients_.clear();

    worker_pool_.shutdown(ThreadPool::ShutdownMode::Graceful);
}

std::string HttpServer::routeKey(HttpMethod method, const std::string& path) {
    return getMethodStr(method) + " " + path;
}

void HttpServer::eventLoop() {
    constexpr int kMaxEvents = 128;
    epoll_event events[kMaxEvents];

    while (running_) {
        const int n = ::epoll_wait(epoll_fd_, events, kMaxEvents, 1000);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (!running_) break;
            LogSync::error("[HttpServer] epoll_wait() failed: ", std::strerror(errno));
            break;
        }

        for (int i = 0; i < n; ++i) {
            const int fd = events[i].data.fd;
            const uint32_t ev = events[i].events;

            if (fd == listen_fd_) {
                handleAccept();
                continue;
            }

            if ((ev & EPOLLERR) || (ev & EPOLLHUP) || (ev & EPOLLRDHUP)) {
                closeClient(fd);
                continue;
            }

            if (ev & EPOLLIN) {
                handleClientReadable(fd);
            }

            if (ev & EPOLLOUT) {
                handleClientWritable(fd);
            }
        }
    }
}

void HttpServer::handleAccept() {
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        const int client_fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (running_) {
                LogSync::error("[HttpServer] accept() failed: ", std::strerror(errno));
            }
            break;
        }

        if (!setNonBlocking(client_fd)) {
            ::close(client_fd);
            continue;
        }

        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLRDHUP;
        ev.data.fd = client_fd;

        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
            ::close(client_fd);
            continue;
        }

        clients_.emplace(client_fd, ClientState{});
    }
}

void HttpServer::handleClientReadable(int client_fd) {
    auto it = clients_.find(client_fd);
    if (it == clients_.end()) return;
    ClientState& st = it->second;

    char buf[4096];
    while (true) {
        const ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            closeClient(client_fd);
            return;
        }

        if (n == 0) {
            closeClient(client_fd);
            return;
        }

        st.inbuf.append(buf, static_cast<size_t>(n));
    }

    std::string request_raw;
    size_t consumed_bytes = 0;
    if (!tryFinalizeRequest(st, request_raw, consumed_bytes)) {
        return;
    }

    Http::Request req;
    std::string parse_err;
    if (!Http::parse_request(request_raw, req, parse_err)) {
        st.outbuf = serializeResponse(makeError(400, "Bad Request", parse_err));
        st.close_after_write = true;
        updateClientEvents(client_fd, EPOLLOUT | EPOLLRDHUP);
        return;
    }

    st.inbuf.erase(0, consumed_bytes);
    st.header_end = std::string::npos;
    st.content_length = 0;
    st.content_length_known = false;
    st.chunked = false;
    st.close_after_write = !should_keep_alive_request(req);

    if (tryHandleWebSocket(client_fd, req)) {
        return;
    }

    Http::Response res = dispatch(req);
    res.headers.set("Connection", st.close_after_write ? "close" : "keep-alive");
    st.outbuf = serializeResponse(res);
    updateClientEvents(client_fd, EPOLLOUT | EPOLLRDHUP);
}

void HttpServer::handleClientWritable(int client_fd) {
    auto it = clients_.find(client_fd);
    if (it == clients_.end()) return;
    ClientState& st = it->second;

    while (!st.outbuf.empty()) {
        const ssize_t n = ::send(client_fd, st.outbuf.data(), st.outbuf.size(), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            closeClient(client_fd);
            return;
        }

        st.outbuf.erase(0, static_cast<size_t>(n));
    }

    if (st.close_after_write) {
        closeClient(client_fd);
        return;
    }

    updateClientEvents(client_fd, EPOLLIN | EPOLLRDHUP);

    if (!st.inbuf.empty()) {
        handleClientReadable(client_fd);
    }
}

void HttpServer::closeClient(int client_fd) {
    if (epoll_fd_ >= 0) {
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
    }
    ::close(client_fd);
    clients_.erase(client_fd);
}

bool HttpServer::updateClientEvents(int client_fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = client_fd;
    return ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd, &ev) == 0;
}

bool HttpServer::setNonBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool HttpServer::setBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == 0;
}

bool HttpServer::tryFinalizeRequest(ClientState& st, std::string& request_raw, size_t& consumed_bytes) {
    request_raw.clear();
    consumed_bytes = 0;
    if (st.header_end == std::string::npos) {
        st.header_end = st.inbuf.find("\r\n\r\n");
        if (st.header_end == std::string::npos) {
            return false;
        }

        const std::string headers = st.inbuf.substr(0, st.header_end + 4);
        st.content_length_known = parse_content_length_from_headers(headers, st.content_length);
        st.chunked = has_chunked_encoding(headers);
    }

    const size_t body_start = st.header_end + 4;
    if (st.content_length_known) {
        consumed_bytes = body_start + st.content_length;
        if (st.inbuf.size() < consumed_bytes) {
            return false;
        }
        request_raw = st.inbuf.substr(0, consumed_bytes);
        return true;
    }

    if (st.chunked) {
        const size_t end = st.inbuf.find("\r\n0\r\n\r\n", body_start);
        if (end == std::string::npos) {
            return false;
        }
        consumed_bytes = end + 7;
        request_raw = st.inbuf.substr(0, consumed_bytes);
        return true;
    }

    consumed_bytes = body_start;
    request_raw = st.inbuf.substr(0, consumed_bytes);
    return true;
}

bool HttpServer::tryHandleWebSocket(int client_fd, const Http::Request& req) {
    const std::string target_path = normalizePath(split_once(req.target, '?')[0]);
    WebSocketHandler ws_handler;
    WebSocketHandshakeHandler ws_handshake_handler;

    {
        std::lock_guard<std::mutex> lock(routes_mutex_);
        auto it = ws_routes_.find(target_path);
        if (it == ws_routes_.end()) return false;
        ws_handler = it->second;

        auto hs_it = ws_handshake_routes_.find(target_path);
        if (hs_it != ws_handshake_routes_.end()) {
            ws_handshake_handler = hs_it->second;
        }
    }

    if (!websocket_is_upgrade_request(req)) {
        auto it = clients_.find(client_fd);
        if (it != clients_.end()) {
            it->second.outbuf = serializeResponse(makeError(400, "Bad Request", "invalid websocket upgrade request"));
            updateClientEvents(client_fd, EPOLLOUT | EPOLLRDHUP);
            return true;
        }
        return false;
    }

    WebSocketServerHandshake handshake;
    if (!websocket_prepare_server_handshake(req, handshake)) {
        auto it = clients_.find(client_fd);
        if (it != clients_.end()) {
            it->second.outbuf = serializeResponse(makeError(500, "Internal Server Error", "websocket accept-key generation failed"));
            updateClientEvents(client_fd, EPOLLOUT | EPOLLRDHUP);
            return true;
        }
        return false;
    }

    if (ws_handshake_handler) {
        const auto reject_response = ws_handshake_handler(req);
        if (reject_response.has_value()) {
            auto it = clients_.find(client_fd);
            if (it != clients_.end()) {
                Http::Response response = reject_response.value();
                if (!response.headers.has("Connection")) {
                    response.headers.set("Connection", "close");
                }
                it->second.outbuf = serializeResponse(response);
                it->second.close_after_write = true;
                updateClientEvents(client_fd, EPOLLOUT | EPOLLRDHUP);
                return true;
            }
            return false;
        }
    }

    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
    clients_.erase(client_fd);

    if (!setBlocking(client_fd)) {
        ::close(client_fd);
        return true;
    }

    try {
        const Http::Response upgrade_response = websocket_build_upgrade_response(handshake);
        worker_pool_.submit(0, [client_fd, ws_handler, upgrade_response, req]() {
            if (!HttpServer::sendAll(client_fd, HttpServer::serializeResponse(upgrade_response))) {
                ::close(client_fd);
                return;
            }

            bool ws_created = false;
            try {
                WebSocketConnection ws(client_fd);
                ws_created = true;
                ws_handler(ws, req);
                if (!ws.isClosed()) {
                    ws.close(1000, "normal closure");
                }
            } catch (...) {
                if (!ws_created) {
                    ::close(client_fd);
                }
            }
        });
    } catch (...) {
        ::close(client_fd);
    }

    return true;
}

bool HttpServer::sendAll(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += static_cast<size_t>(n);
    }

    return true;
}

Http::Response HttpServer::dispatch(const Http::Request& req) {
    const std::string target_path = normalizePath(split_once(req.target, '?')[0]);

    HttpMethod method = HttpMethod::END;
    const std::string method_lower = to_lower(req.method);

    if (method_lower == "get") method = HttpMethod::GET;
    else if (method_lower == "post") method = HttpMethod::POST;
    else if (method_lower == "put") method = HttpMethod::PUT;
    else if (method_lower == "delete") method = HttpMethod::DELETE;
    else if (method_lower == "patch") method = HttpMethod::PATCH;
    else if (method_lower == "head") method = HttpMethod::HEAD;
    else if (method_lower == "options") method = HttpMethod::OPTIONS;
    else if (method_lower == "connect") method = HttpMethod::CONNECT;

    if (method == HttpMethod::END) {
        return makeError(405, "Method Not Allowed", "unsupported method");
    }

    const std::string key = routeKey(method, target_path);

    Handler handler;
    std::unordered_map<std::string, std::string> path_params;
    {
        std::lock_guard<std::mutex> lock(routes_mutex_);
        auto it = routes_.find(key);
        if (it != routes_.end()) {
            handler = it->second;
        } else {
            const std::string method_prefix = getMethodStr(method) + " ";
            bool matched = false;
            for (const auto& [route_key, route_handler] : routes_) {
                if (route_key.rfind(method_prefix, 0) != 0) continue;

                const std::string pattern = route_key.substr(method_prefix.size());
                if (pattern.find('{') == std::string::npos || pattern.find('}') == std::string::npos) continue;

                std::unordered_map<std::string, std::string> candidate_params;
                if (!match_route_pattern(pattern, target_path, candidate_params)) continue;

                handler = route_handler;
                path_params = std::move(candidate_params);
                matched = true;
                break;
            }

            if (!matched) {
                return makeError(404, "Not Found", "route not found");
            }
        }
    }

    try {
        Http::Request req_with_params = req;
        req_with_params.path_params = std::move(path_params);

        Middleware middleware;
        {
            std::lock_guard<std::mutex> lock(routes_mutex_);
            middleware = middleware_;
        }
        if (middleware) {
            const auto rejection = middleware(req_with_params);
            if (rejection) {
                Http::Response response = *rejection;
                if (response.status_code <= 0) {
                    response.status_code = 500;
                }
                if (response.reason.empty()) {
                    response.reason = default_reason_phrase(response.status_code);
                }
                return response;
            }
        }

        auto response = handler(req_with_params);
        if (response.status_code <= 0) {
            response.status_code = 200;
        }
        if (response.reason.empty()) {
            response.reason = default_reason_phrase(response.status_code);
        }
        return response;
    } catch (const std::exception& e) {
        return makeError(500, "Internal Server Error", e.what());
    } catch (...) {
        return makeError(500, "Internal Server Error", "unknown exception");
    }
}

Http::Response HttpServer::makeError(int code, const std::string& reason, const std::string& body) {
    Http::Response res;
    res.version = "HTTP/1.1";
    res.status_code = code;
    res.reason = reason.empty() ? default_reason_phrase(code) : reason;
    res.body = body;
    res.headers.set("Content-Type", "text/plain; charset=utf-8");
    res.headers.set("Connection", "close");

    return res;
}

std::string HttpServer::serializeResponse(const Http::Response& response) {
    Http::Response res = response;

    if (res.version.empty()) res.version = "HTTP/1.1";
    if (res.status_code <= 0) res.status_code = 200;
    if (res.reason.empty()) res.reason = default_reason_phrase(res.status_code);

    if (!res.headers.has("Content-Length")) {
        res.headers.set("Content-Length", std::to_string(res.body.size()));
    }

    if (!res.headers.has("Connection")) {
        res.headers.set("Connection", "close");
    }

    std::ostringstream oss;
    oss << res.version << " " << res.status_code << " " << res.reason << "\r\n";

    for (const auto& [key, value] : res.headers.kv) {
        oss << key << ": " << value << "\r\n";
    }

    oss << "\r\n";
    oss << res.body;

    return oss.str();
}

std::string HttpServer::normalizePath(std::string path) {
    trim_inplace(path);
    if (path.empty()) return "/";
    if (path[0] != '/') path.insert(path.begin(), '/');

    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }

    return path;
}
