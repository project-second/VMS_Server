#ifndef __WEBSOCKET_H__
#define __WEBSOCKET_H__

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "HttpParser.h"

struct WebSocketFrame {
    bool fin = true;
    uint8_t opcode = 0;
    std::vector<uint8_t> payload;
};

struct WebSocketServerHandshake {
    std::string accept_key;
    std::string subprotocol;
};

struct WebSocketConnectionState;

class WebSocketConnection {
public:
    explicit WebSocketConnection(int fd);
    ~WebSocketConnection();

    WebSocketConnection(const WebSocketConnection&) = delete;
    WebSocketConnection& operator=(const WebSocketConnection&) = delete;

    bool recvFrame(WebSocketFrame& frame);
    bool recvText(std::string& out);

    bool sendText(const std::string& text);
    bool sendBinary(const std::vector<uint8_t>& data);
    bool sendPong(const std::vector<uint8_t>& payload = {});

    void close(uint16_t code = 1000, const std::string& reason = "");
    bool isClosed() const;

private:
    std::unique_ptr<WebSocketConnectionState> state_;
};

bool websocket_is_upgrade_request(const std::string& method,
    const std::string* connection_header,
    const std::string* upgrade_header,
    const std::string* sec_key_header);
bool websocket_is_upgrade_request(const Http::Request& request);
bool websocket_is_upgrade_response(const Http::Response& response);

bool websocket_make_accept_key(const std::string& client_key, std::string& out_accept_key);
bool websocket_generate_client_key(std::string& out_client_key);
bool websocket_validate_accept_key(const std::string& client_key, const std::string& server_accept_key);
bool websocket_prepare_client_headers(Http::Headers& headers, std::string& out_client_key,
    const std::string& subprotocol = "");
bool websocket_validate_server_response(const Http::Response& response,
    const std::string& client_key, const std::string& expected_subprotocol = "");
std::string websocket_select_subprotocol(const std::string* requested_protocol_header);
bool websocket_prepare_server_handshake(const Http::Request& request,
    WebSocketServerHandshake& out_handshake);
Http::Response websocket_build_upgrade_response(const WebSocketServerHandshake& handshake);

class WebSocketClientConnection {
public:
    explicit WebSocketClientConnection(int fd);
    ~WebSocketClientConnection();

    WebSocketClientConnection(const WebSocketClientConnection&) = delete;
    WebSocketClientConnection& operator=(const WebSocketClientConnection&) = delete;

    bool recvFrame(WebSocketFrame& frame);
    bool recvText(std::string& out);

    bool sendText(const std::string& text);
    bool sendBinary(const std::vector<uint8_t>& data);
    bool sendPing(const std::vector<uint8_t>& payload = {});
    bool sendPong(const std::vector<uint8_t>& payload = {});

    void close(uint16_t code = 1000, const std::string& reason = "");
    bool isClosed() const;

private:
    std::unique_ptr<WebSocketConnectionState> state_;
};

#endif // __WEBSOCKET_H__
