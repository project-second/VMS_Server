#ifndef __HTTPSERVER_H__
#define __HTTPSERVER_H__

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include "HttpCodes.h"
#include "HttpParser.h"
#include "WebSocket.h"
#include "StringHelpers.h"
#include "ThreadPool.hpp"

class HttpServer {
public:
    using Handler = std::function<Http::Response(const Http::Request&)>;
    using WebSocketHandler = std::function<void(WebSocketConnection&, const Http::Request&)>;
    using WebSocketHandshakeHandler = std::function<std::optional<Http::Response>(const Http::Request&)>;
    using Middleware = std::function<std::optional<Http::Response>(Http::Request&)>;

    explicit HttpServer(std::string bind_ip, int port,
        std::size_t worker_count = std::thread::hardware_concurrency());
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void setMiddleware(Middleware middleware);

    bool addRoute(HttpMethod method, std::string path, Handler handler);
    bool removeRoute(HttpMethod method, std::string path);
    bool addWebSocketRoute(
        std::string path,
        WebSocketHandler handler,
        WebSocketHandshakeHandler handshake_handler = nullptr);
    bool removeWebSocketRoute(std::string path);

    bool start();
    void stop();

private:
    struct ClientState {
        std::string inbuf;
        std::string outbuf;

        size_t header_end = std::string::npos;
        size_t content_length = 0;
        bool content_length_known = false;
        bool chunked = false;
        bool close_after_write = true;
    };

private:
    std::string bind_ip_;
    int port_;

    int listen_fd_ = -1;
    int epoll_fd_ = -1;

    std::thread io_thread_;
    std::atomic<bool> running_{false};

    ThreadPool worker_pool_;

    std::mutex routes_mutex_;
    std::unordered_map<std::string, Handler> routes_;
    std::unordered_map<std::string, WebSocketHandler> ws_routes_;
    std::unordered_map<std::string, WebSocketHandshakeHandler> ws_handshake_routes_;
    Middleware middleware_;

    std::unordered_map<int, ClientState> clients_;

private:
    static std::string routeKey(HttpMethod method, const std::string& path);

    void eventLoop();
    void handleAccept();
    void handleClientReadable(int client_fd);
    void handleClientWritable(int client_fd);
    void closeClient(int client_fd);

    bool updateClientEvents(int client_fd, uint32_t events);

    static bool setNonBlocking(int fd);
    static bool setBlocking(int fd);

    bool tryFinalizeRequest(ClientState& st, std::string& request_raw, size_t& consumed_bytes);
    bool tryHandleWebSocket(int client_fd, const Http::Request& req);

    static bool sendAll(int fd, const std::string& data);

    Http::Response dispatch(const Http::Request& req);

    static Http::Response makeError(int code, const std::string& reason, const std::string& body = "");
    static std::string serializeResponse(const Http::Response& response);
    static std::string normalizePath(std::string path);
};

#endif // __HTTPSERVER_H__
