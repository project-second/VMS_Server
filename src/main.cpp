#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

constexpr int kBacklog = 16;
volatile std::sig_atomic_t g_stop = 0;

void throw_errno(const std::string& message);

void on_signal(int) {
    g_stop = 1;
}

void install_signal_handlers() {
    struct sigaction action{};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGINT, &action, nullptr) < 0) {
        throw_errno("sigaction(SIGINT) failed");
    }
    if (sigaction(SIGTERM, &action, nullptr) < 0) {
        throw_errno("sigaction(SIGTERM) failed");
    }
}

class Socket {
public:
    explicit Socket(int fd = -1) : fd_(fd) {}
    ~Socket() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) {
                close(fd_);
            }
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    int get() const {
        return fd_;
    }

private:
    int fd_;
};

void throw_errno(const std::string& message) {
    throw std::runtime_error(message + ": " + std::strerror(errno));
}

void send_all(int fd, const std::string& data) {
    const char* cursor = data.data();
    std::size_t remaining = data.size();

    while (remaining > 0) {
        const ssize_t sent = send(fd, cursor, remaining, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw_errno("send failed");
        }
        cursor += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
}

std::string recv_some(int fd) {
    char buffer[4096];
    const ssize_t received = recv(fd, buffer, sizeof(buffer) - 1, 0);
    if (received < 0) {
        throw_errno("recv failed");
    }
    buffer[received] = '\0';
    return std::string(buffer);
}

Socket connect_to_host(const std::string& host, const std::string& port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* raw_results = nullptr;
    const int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &raw_results);
    if (rc != 0) {
        throw std::runtime_error(std::string("getaddrinfo failed: ") + gai_strerror(rc));
    }

    Socket result;
    for (addrinfo* item = raw_results; item != nullptr; item = item->ai_next) {
        Socket candidate(socket(item->ai_family, item->ai_socktype, item->ai_protocol));
        if (candidate.get() < 0) {
            continue;
        }

        if (connect(candidate.get(), item->ai_addr, item->ai_addrlen) == 0) {
            result = std::move(candidate);
            break;
        }
    }

    freeaddrinfo(raw_results);

    if (result.get() < 0) {
        throw_errno("connect failed");
    }

    return result;
}

void run_server(int port) {
    Socket server_fd(socket(AF_INET, SOCK_STREAM, 0));
    if (server_fd.get() < 0) {
        throw_errno("socket failed");
    }

    int reuse = 1;
    if (setsockopt(server_fd.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        throw_errno("setsockopt failed");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(server_fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        throw_errno("bind failed");
    }

    if (listen(server_fd.get(), kBacklog) < 0) {
        throw_errno("listen failed");
    }

    std::cout << "C++ backend server listening on http://127.0.0.1:" << port << '\n';
    std::cout << "Run `./nginx_sample nginx-conf` to print an Nginx proxy config.\n";

    while (!g_stop) {
        sockaddr_in client_address{};
        socklen_t client_length = sizeof(client_address);
        Socket client_fd(accept(server_fd.get(), reinterpret_cast<sockaddr*>(&client_address), &client_length));
        if (client_fd.get() < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw_errno("accept failed");
        }

        const std::string request = recv_some(client_fd.get());
        const std::string first_line = request.substr(0, request.find("\r\n"));

        std::ostringstream body;
        body << "{\n"
             << "  \"message\": \"Hello from a C++ backend behind Nginx\",\n"
             << "  \"request\": \"" << first_line << "\"\n"
             << "}\n";

        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Content-Length: " << body.str().size() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << body.str();

        send_all(client_fd.get(), response.str());
    }
}

void run_client(const std::string& host, const std::string& port, const std::string& path) {
    Socket fd = connect_to_host(host, port);

    std::ostringstream request;
    request << "GET " << path << " HTTP/1.1\r\n"
            << "Host: " << host << "\r\n"
            << "User-Agent: nginx-cpp-sample/1.0\r\n"
            << "Accept: */*\r\n"
            << "Connection: close\r\n"
            << "\r\n";

    send_all(fd.get(), request.str());

    for (;;) {
        const std::string chunk = recv_some(fd.get());
        if (chunk.empty()) {
            break;
        }
        std::cout << chunk;
    }
}

void print_nginx_config(int nginx_port, int backend_port) {
    std::cout
        << "server {\n"
        << "    listen " << nginx_port << ";\n"
        << "    server_name localhost;\n"
        << "\n"
        << "    location / {\n"
        << "        proxy_pass http://127.0.0.1:" << backend_port << ";\n"
        << "        proxy_http_version 1.1;\n"
        << "        proxy_set_header Host $host;\n"
        << "        proxy_set_header X-Real-IP $remote_addr;\n"
        << "        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;\n"
        << "    }\n"
        << "}\n";
}

void print_usage(const char* program) {
    std::cerr
        << "Usage:\n"
        << "  " << program << " server [backend_port]\n"
        << "  " << program << " client [host] [port] [path]\n"
        << "  " << program << " nginx-conf [nginx_port] [backend_port]\n"
        << "\n"
        << "Example:\n"
        << "  g++ -std=c++17 -Wall -Wextra -pedantic src/main.cpp -o nginx_sample\n"
        << "  ./nginx_sample server 18080\n"
        << "  ./nginx_sample nginx-conf 8080 18080\n"
        << "  ./nginx_sample client 127.0.0.1 8080 /\n";
}

int parse_port(const char* value) {
    const int port = std::stoi(value);
    if (port <= 0 || port > 65535) {
        throw std::runtime_error("port must be in range 1..65535");
    }
    return port;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        install_signal_handlers();

        if (argc < 2) {
            print_usage(argv[0]);
            return 1;
        }

        const std::string mode = argv[1];
        if (mode == "server") {
            run_server(argc >= 3 ? parse_port(argv[2]) : 18080);
            return 0;
        }

        if (mode == "client") {
            const std::string host = argc >= 3 ? argv[2] : "127.0.0.1";
            const std::string port = argc >= 4 ? argv[3] : "8080";
            const std::string path = argc >= 5 ? argv[4] : "/";
            run_client(host, port, path);
            return 0;
        }

        if (mode == "nginx-conf") {
            const int nginx_port = argc >= 3 ? parse_port(argv[2]) : 8080;
            const int backend_port = argc >= 4 ? parse_port(argv[3]) : 18080;
            print_nginx_config(nginx_port, backend_port);
            return 0;
        }

        print_usage(argv[0]);
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
