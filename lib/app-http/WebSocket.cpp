#include "WebSocket.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <sys/socket.h>
#include <unistd.h>

#include "HttpCodes.h"
#include "StringHelpers.h"

struct WebSocketConnectionState {
    explicit WebSocketConnectionState(int fd_, bool mask_outgoing_)
        : fd(fd_), mask_outgoing(mask_outgoing_) {}

    int fd;
    bool closed = false;
    bool mask_outgoing = false;
    std::mutex send_mutex;
};

namespace {

constexpr const char* kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

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

std::vector<uint8_t> to_bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

bool make_random_mask(uint8_t mask[4]) {
    return RAND_bytes(mask, 4) == 1;
}

bool recv_n(int fd, void* dst, size_t n) {
    uint8_t* p = static_cast<uint8_t*>(dst);
    size_t got = 0;

    while (got < n) {
        const ssize_t r = ::recv(fd, p + got, n - got, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (r == 0) return false;
        got += static_cast<size_t>(r);
    }

    return true;
}

bool send_all_raw(int fd, const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t sent = 0;

    while (sent < n) {
        const ssize_t w = ::send(fd, p + sent, n - sent, 0);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += static_cast<size_t>(w);
    }

    return true;
}

bool websocket_recv_frame_impl(WebSocketConnectionState& state, WebSocketFrame& frame) {
    frame = WebSocketFrame{};

    uint8_t h[2];
    if (!recv_n(state.fd, h, sizeof(h))) return false;

    frame.fin = (h[0] & 0x80) != 0;
    frame.opcode = (h[0] & 0x0F);

    const bool masked = (h[1] & 0x80) != 0;
    uint64_t payload_len = (h[1] & 0x7F);

    if (payload_len == 126) {
        uint8_t ext[2];
        if (!recv_n(state.fd, ext, sizeof(ext))) return false;
        payload_len = (static_cast<uint64_t>(ext[0]) << 8) |
            static_cast<uint64_t>(ext[1]);
    } else if (payload_len == 127) {
        uint8_t ext[8];
        if (!recv_n(state.fd, ext, sizeof(ext))) return false;
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | static_cast<uint64_t>(ext[i]);
        }
    }

    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked && !recv_n(state.fd, mask, sizeof(mask))) return false;

    frame.payload.resize(static_cast<size_t>(payload_len));
    if (payload_len > 0 && !recv_n(state.fd, frame.payload.data(), frame.payload.size())) return false;

    if (masked) {
        for (size_t i = 0; i < frame.payload.size(); ++i) {
            frame.payload[i] ^= mask[i % 4];
        }
    }

    if (frame.opcode == 0x8) {
        state.closed = true;
    }

    return true;
}

bool websocket_send_frame_impl(WebSocketConnectionState& state,
    uint8_t opcode, const std::vector<uint8_t>& payload, bool fin) {
    std::lock_guard<std::mutex> lock(state.send_mutex);
    if (state.closed) return false;

    std::vector<uint8_t> frame;
    frame.reserve((state.mask_outgoing ? 18 : 14) + payload.size());

    const uint8_t b0 = static_cast<uint8_t>((fin ? 0x80 : 0x00) | (opcode & 0x0F));
    frame.push_back(b0);

    const uint64_t len = payload.size();
    const uint8_t mask_bit = state.mask_outgoing ? 0x80 : 0x00;
    if (len <= 125) {
        frame.push_back(static_cast<uint8_t>(mask_bit | len));
    } else if (len <= 0xFFFF) {
        frame.push_back(static_cast<uint8_t>(mask_bit | 126));
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(static_cast<uint8_t>(mask_bit | 127));
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (8 * i)) & 0xFF));
        }
    }

    if (state.mask_outgoing) {
        uint8_t mask[4];
        if (!make_random_mask(mask)) return false;
        frame.insert(frame.end(), mask, mask + sizeof(mask));

        const size_t payload_offset = frame.size();
        frame.resize(payload_offset + payload.size());
        for (size_t i = 0; i < payload.size(); ++i) {
            frame[payload_offset + i] = static_cast<uint8_t>(payload[i] ^ mask[i % 4]);
        }
    } else {
        frame.insert(frame.end(), payload.begin(), payload.end());
    }

    return send_all_raw(state.fd, frame.data(), frame.size());
}

bool websocket_recv_text_impl(WebSocketConnectionState& state, std::string& out) {
    out.clear();

    while (!state.closed) {
        WebSocketFrame frame;
        if (!websocket_recv_frame_impl(state, frame)) return false;

        if (frame.opcode == 0x1) {
            out.assign(frame.payload.begin(), frame.payload.end());
            return true;
        }

        if (frame.opcode == 0x8) {
            std::vector<uint8_t> payload = {0x03, 0xE8, 'n','o','r','m','a','l',' ','c','l','o','s','u','r','e'};
            websocket_send_frame_impl(state, 0x8, payload, true);
            ::shutdown(state.fd, SHUT_RDWR);
            ::close(state.fd);
            state.fd = -1;
            state.closed = true;
            return false;
        }

        if (frame.opcode == 0x9) {
            if (!websocket_send_frame_impl(state, 0xA, frame.payload, true)) {
                return false;
            }
        }
    }

    return false;
}

void websocket_close_impl(WebSocketConnectionState& state, uint16_t code, const std::string& reason) {
    if (state.closed) return;

    std::vector<uint8_t> payload;
    payload.reserve(2 + reason.size());
    payload.push_back(static_cast<uint8_t>((code >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(code & 0xFF));
    payload.insert(payload.end(), reason.begin(), reason.end());

    websocket_send_frame_impl(state, 0x8, payload, true);

    ::shutdown(state.fd, SHUT_RDWR);
    ::close(state.fd);
    state.fd = -1;
    state.closed = true;
}

} // namespace

WebSocketConnection::WebSocketConnection(int fd)
    : state_(std::make_unique<WebSocketConnectionState>(fd, false)) {}

WebSocketConnection::~WebSocketConnection() {
    if (!state_->closed && state_->fd >= 0) {
        ::close(state_->fd);
        state_->fd = -1;
        state_->closed = true;
    }
}

bool WebSocketConnection::recvFrame(WebSocketFrame& frame) {
    return websocket_recv_frame_impl(*state_, frame);
}

bool WebSocketConnection::recvText(std::string& out) {
    return websocket_recv_text_impl(*state_, out);
}

bool WebSocketConnection::sendText(const std::string& text) {
    return websocket_send_frame_impl(*state_, 0x1, to_bytes(text), true);
}

bool WebSocketConnection::sendBinary(const std::vector<uint8_t>& data) {
    return websocket_send_frame_impl(*state_, 0x2, data, true);
}

bool WebSocketConnection::sendPong(const std::vector<uint8_t>& payload) {
    return websocket_send_frame_impl(*state_, 0xA, payload, true);
}

void WebSocketConnection::close(uint16_t code, const std::string& reason) {
    websocket_close_impl(*state_, code, reason);
}

bool WebSocketConnection::isClosed() const {
    return state_->closed;
}

bool websocket_is_upgrade_request(const std::string& method,
    const std::string* connection_header,
    const std::string* upgrade_header,
    const std::string* sec_key_header) {

    if (to_lower(method) != "get") return false;
    if (connection_header == nullptr || upgrade_header == nullptr || sec_key_header == nullptr) return false;

    if (!contains_token_ci(*connection_header, "upgrade")) return false;
    if (to_lower(trim(*upgrade_header)) != "websocket") return false;

    return !trim(*sec_key_header).empty();
}

bool websocket_is_upgrade_request(const Http::Request& request) {
    return websocket_is_upgrade_request(
        request.method,
        request.headers.get("connection"),
        request.headers.get("upgrade"),
        request.headers.get("sec-websocket-key"));
}

bool websocket_is_upgrade_response(const Http::Response& response) {
    if (response.status_code != 101) return false;

    const std::string* connection = response.headers.get("connection");
    const std::string* upgrade = response.headers.get("upgrade");
    if (connection == nullptr || upgrade == nullptr) return false;

    if (!contains_token_ci(*connection, "upgrade")) return false;
    return to_lower(trim(*upgrade)) == "websocket";
}

bool websocket_make_accept_key(const std::string& client_key, std::string& out_accept_key) {
    out_accept_key.clear();

    std::string src = trim(client_key) + kWebSocketGuid;

    unsigned char sha1[SHA_DIGEST_LENGTH];
    if (SHA1(reinterpret_cast<const unsigned char*>(src.data()), src.size(), sha1) == nullptr) {
        return false;
    }

    std::string encoded;
    encoded.resize(4 * ((SHA_DIGEST_LENGTH + 2) / 3));

    const int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&encoded[0]), sha1, SHA_DIGEST_LENGTH);
    if (n <= 0) return false;

    encoded.resize(static_cast<size_t>(n));
    out_accept_key = std::move(encoded);
    return true;
}

bool websocket_generate_client_key(std::string& out_client_key) {
    out_client_key.clear();

    unsigned char nonce[16];
    if (RAND_bytes(nonce, sizeof(nonce)) != 1) {
        return false;
    }

    std::string encoded;
    encoded.resize(4 * ((sizeof(nonce) + 2) / 3));

    const int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&encoded[0]), nonce, sizeof(nonce));
    if (n <= 0) return false;

    encoded.resize(static_cast<size_t>(n));
    out_client_key = std::move(encoded);
    return true;
}

bool websocket_validate_accept_key(const std::string& client_key, const std::string& server_accept_key) {
    std::string expected;
    if (!websocket_make_accept_key(client_key, expected)) return false;
    return trim(expected) == trim(server_accept_key);
}

bool websocket_prepare_client_headers(Http::Headers& headers, std::string& out_client_key,
    const std::string& subprotocol) {
    if (!websocket_generate_client_key(out_client_key)) {
        return false;
    }

    headers.set("Upgrade", "websocket");
    headers.set("Connection", "Upgrade");
    headers.set("Sec-WebSocket-Version", "13");
    headers.set("Sec-WebSocket-Key", out_client_key);
    if (!subprotocol.empty()) {
        headers.set("Sec-WebSocket-Protocol", subprotocol);
    } else {
        headers.kv.erase("sec-websocket-protocol");
    }

    return true;
}

bool websocket_validate_server_response(const Http::Response& response,
    const std::string& client_key,
    const std::string& expected_subprotocol) {
    if (!websocket_is_upgrade_response(response)) return false;

    const std::string* accept_key = response.headers.get("sec-websocket-accept");
    if (accept_key == nullptr || !websocket_validate_accept_key(client_key, *accept_key)) {
        return false;
    }

    const std::string* accepted_protocol = response.headers.get("sec-websocket-protocol");
    if (!expected_subprotocol.empty() &&
        accepted_protocol != nullptr &&
        trim(*accepted_protocol) != expected_subprotocol) {
        return false;
    }

    return true;
}

std::string websocket_select_subprotocol(const std::string* requested_protocol_header) {
    if (requested_protocol_header == nullptr) return "";

    std::string ws_protocol = trim(*requested_protocol_header);
    if (ws_protocol.empty()) return "";

    const auto comma_pos = ws_protocol.find(',');
    if (comma_pos != std::string::npos) {
        ws_protocol = trim(ws_protocol.substr(0, comma_pos));
    }

    return ws_protocol;
}

bool websocket_prepare_server_handshake(const Http::Request& request,
    WebSocketServerHandshake& out_handshake) {
    out_handshake = WebSocketServerHandshake{};

    if (!websocket_is_upgrade_request(request)) {
        return false;
    }

    const std::string* client_key = request.headers.get("sec-websocket-key");
    if (client_key == nullptr || !websocket_make_accept_key(*client_key, out_handshake.accept_key)) {
        return false;
    }

    out_handshake.subprotocol = websocket_select_subprotocol(request.headers.get("sec-websocket-protocol"));
    return true;
}

Http::Response websocket_build_upgrade_response(const WebSocketServerHandshake& handshake) {
    Http::Response response;
    response.version = "HTTP/1.1";
    response.setStatus(HttpCode::Switching_Protocol);
    response.headers.set("Upgrade", "websocket");
    response.headers.set("Connection", "Upgrade");
    response.headers.set("Sec-WebSocket-Accept", handshake.accept_key);
    if (!handshake.subprotocol.empty()) {
        response.headers.set("Sec-WebSocket-Protocol", handshake.subprotocol);
    }
    return response;
}

WebSocketClientConnection::WebSocketClientConnection(int fd)
    : state_(std::make_unique<WebSocketConnectionState>(fd, true)) {}

WebSocketClientConnection::~WebSocketClientConnection() {
    if (!state_->closed && state_->fd >= 0) {
        ::close(state_->fd);
        state_->fd = -1;
        state_->closed = true;
    }
}

bool WebSocketClientConnection::recvFrame(WebSocketFrame& frame) {
    return websocket_recv_frame_impl(*state_, frame);
}

bool WebSocketClientConnection::recvText(std::string& out) {
    return websocket_recv_text_impl(*state_, out);
}

bool WebSocketClientConnection::sendText(const std::string& text) {
    return websocket_send_frame_impl(*state_, 0x1, to_bytes(text), true);
}

bool WebSocketClientConnection::sendBinary(const std::vector<uint8_t>& data) {
    return websocket_send_frame_impl(*state_, 0x2, data, true);
}

bool WebSocketClientConnection::sendPing(const std::vector<uint8_t>& payload) {
    return websocket_send_frame_impl(*state_, 0x9, payload, true);
}

bool WebSocketClientConnection::sendPong(const std::vector<uint8_t>& payload) {
    return websocket_send_frame_impl(*state_, 0xA, payload, true);
}

void WebSocketClientConnection::close(uint16_t code, const std::string& reason) {
    websocket_close_impl(*state_, code, reason);
}

bool WebSocketClientConnection::isClosed() const {
    return state_->closed;
}
