#include "HttpParser.h"

/* private */

static std::optional<uint16_t> parse_port(const std::string& s) {
    if (s.empty()) return std::nullopt;
    unsigned long v = 0;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
        v = v * 10 + static_cast<unsigned>(c - '0');
        if (v > 65535UL) return std::nullopt;
    }
    return static_cast<uint16_t>(v);
}

inline bool decode_chunked(const std::string& in, std::string& out) {
    out.clear();
    size_t i = 0;

    auto read_line = [&](std::string& line) -> bool {
        auto pos = in.find("\r\n", i);
        if (pos == std::string::npos) return false;
        line = in.substr(i, pos - i);
        i = pos + 2;
        return true;
    };

    while (true) {
        std::string line;
        if (!read_line(line)) return false;

        // chunk-size may have extensions: "1A;ext=..."
        auto semi = line.find(';');
        std::string size_hex = (semi == std::string::npos) ? line : line.substr(0, semi);
        trim_inplace(size_hex);
        if (size_hex.empty()) return false;

        char* endp = nullptr;
        unsigned long chunk_size = std::strtoul(size_hex.c_str(), &endp, 16);
        if (endp == size_hex.c_str()) return false;

        if (chunk_size == 0) {
        // read trailing headers until CRLF
        while (true) {
            std::string trailer;
            if (!read_line(trailer)) return false;
            if (trailer.empty()) break;
        }
        return true;
        }

        if (i + chunk_size > in.size()) return false;
        out.append(in.data() + i, chunk_size);
        i += chunk_size;

        // each chunk ends with CRLF
        if (i + 2 > in.size() || in.compare(i, 2, "\r\n") != 0) return false;
        i += 2;
    }
}

// ------------------------------
// Core: parse raw HTTP message into start-line + headers + body
// ------------------------------

struct ParseCommonResult {
    std::string start_line;
    Http::Headers headers;
    std::string body_raw;     // body as found after header delimiter
    bool is_chunked = false;
    std::optional<size_t> content_length;
};

inline std::unordered_map<std::string, std::string> parse_query_params(const std::string& target) {
    std::unordered_map<std::string, std::string> query;

    const std::size_t qpos = target.find('?');
    if (qpos == std::string::npos || qpos + 1 >= target.size()) {
        return query;
    }

    const std::string query_raw = target.substr(qpos + 1);
    std::size_t begin = 0;
    while (begin <= query_raw.size()) {
        std::size_t end = query_raw.find('&', begin);
        if (end == std::string::npos) end = query_raw.size();

        const std::string token = query_raw.substr(begin, end - begin);
        if (!token.empty()) {
            const auto kv = split_once(token, '=');
            const std::string key = url_decode(kv.front());
            const std::string value = (kv.size() > 1) ? url_decode(kv.back()) : "";
            if (!key.empty()) query[key] = value;
        }

        if (end == query_raw.size()) break;
        begin = end + 1;
    }

    return query;
}

inline std::optional<size_t> parse_content_length(const Http::Headers& h) {
    auto* v = h.get("content-length");
    if (!v) return std::nullopt;

    std::string s = *v;
    trim_inplace(s);
    if (s.empty()) return std::nullopt;

    char* endp = nullptr;
    unsigned long n = std::strtoul(s.c_str(), &endp, 10);
    if (endp == s.c_str()) return std::nullopt;

    return static_cast<size_t>(n);
}

inline bool parse_common(const std::string& raw, ParseCommonResult& out, std::string& err) {
    out = ParseCommonResult{};
    err.clear();

    // Find header end: CRLFCRLF (preferred) or LFLF (fallback)
    size_t header_end = raw.find("\r\n\r\n");
    size_t delim_len = 4;
    if (header_end == std::string::npos) {
        header_end = raw.find("\n\n");
        delim_len = 2;
    }

    if (header_end == std::string::npos) {
        err = "HTTP header delimiter not found";
        return false;
    }

    std::string header_part = raw.substr(0, header_end);
    out.body_raw = raw.substr(header_end + delim_len);

    // Split lines
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < header_part.size()) {
        size_t eol = header_part.find('\n', pos);
        if (eol == std::string::npos) eol = header_part.size();
        std::string line = header_part.substr(pos, eol - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        pos = (eol == header_part.size()) ? eol : (eol + 1);
    }
    if (lines.empty() || lines[0].empty()) {
        err = "Empty start-line";
        return false;
    }

    out.start_line = lines[0];

    // Headers (simple; supports obs-fold? we ignore multiline continuation)
    for (size_t i = 1; i < lines.size(); ++i) {
        const auto& ln = lines[i];
        if (ln.empty()) continue;
        auto parts = split_once(ln, ':');
        if (parts.size() == 1) continue; // invalid header line -> ignore
        std::string key = parts[0];
        std::string val = parts[1];
        trim_inplace(key);
        trim_inplace(val);
        if (!key.empty()) out.headers.set(std::move(key), std::move(val));
    }

    // chunked?
    if (auto* te = out.headers.get("transfer-encoding")) {
        std::string te_l = to_lower(*te);
        if (te_l.find("chunked") != std::string::npos) out.is_chunked = true;
    }

    out.content_length = parse_content_length(out.headers);
    return true;
}


/* public */

std::optional<Http::ParsedUrl> Http::parse_http_https_url(std::string url) {
    trim_inplace(url);
    if (url.empty()) return std::nullopt;

    // 1) scheme:// 분리
    auto scheme_pos = url.find("://");
    if (scheme_pos == std::string::npos) return std::nullopt;

    Http::ParsedUrl out{};
    out.scheme = url.substr(0, scheme_pos);
    for (auto& ch : out.scheme) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    if (out.scheme != "http" && out.scheme != "https") return std::nullopt;

    const uint16_t default_port = (out.scheme == "https") ? 443 : 80;

    std::string rest = url.substr(scheme_pos + 3); // after ://
    if (rest.empty()) return std::nullopt;

    // 2) authority(host[:port]) 와 path(/...) 분리
    //    authority는 '/' 전까지
    std::string authority;
    auto slash_pos = rest.find('/');
    if (slash_pos == std::string::npos) {
        authority = rest;
        out.path = "/"; // path 없으면 "/"로
    } else {
        authority = rest.substr(0, slash_pos);
        out.path = rest.substr(slash_pos);
        if (out.path.empty()) out.path = "/";
    }

    if (authority.empty()) return std::nullopt;

    // 3) host, port 파싱
    // IPv6는 [::1]:80 형태를 고려 (필요없으면 제거 가능)
    if (!authority.empty() && authority.front() == '[') {
        // [IPv6]
        auto close = authority.find(']');
        if (close == std::string::npos) return std::nullopt;
        out.host = authority.substr(1, close - 1);

        uint16_t port = default_port;
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') return std::nullopt;
            auto p = parse_port(authority.substr(close + 2));
            if (!p) return std::nullopt;
            port = *p;
        }
        out.port = port;
        return out;
    }

    // 일반 host[:port]
    // 마지막 ':' 기준으로 포트인지 확인 (hostname에 ':'가 들어가는 경우는 IPv6라 []로 와야 안전)
    uint16_t port = default_port;
    auto colon = authority.rfind(':');
    if (colon != std::string::npos) {
        // host:port 형태로 간주 (port가 숫자일 때만)
        std::string host_part = authority.substr(0, colon);
        std::string port_part = authority.substr(colon + 1);

        auto p = parse_port(port_part);
        if (p) {
            // ✅ 명시 포트가 있으면 우선 사용
            out.host = host_part;
            port = *p;
        } else {
            // 콜론이 있지만 뒤가 숫자가 아니면 host로 취급 (예: "example.com:abc" 같은 이상 케이스)
            out.host = authority;
        }
    } else {
        out.host = authority;
    }

    if (out.host.empty()) return std::nullopt;
    out.port = port;
    return out;
}

bool Http::parse_request(const std::string& raw, Request& req, std::string& err) {
    ParseCommonResult c;
    if (!parse_common(raw, c, err)) return false;

    // Start-line: METHOD SP TARGET SP HTTP/VERSION
    // Example: POST /onvif/device_service HTTP/1.1
    auto a = c.start_line.find(' ');
    auto b = (a == std::string::npos) ? std::string::npos : c.start_line.find(' ', a + 1);
    if (a == std::string::npos || b == std::string::npos) {
        err = "Invalid request start-line";
        return false;
    }

    req = Request{};
    req.method = c.start_line.substr(0, a);
    req.target = c.start_line.substr(a + 1, b - (a + 1));
    req.version = c.start_line.substr(b + 1);
    trim_inplace(req.method);
    trim_inplace(req.target);
    trim_inplace(req.version);
    req.headers = std::move(c.headers);
    req.query_params = parse_query_params(req.target);

    // Body finalize
    if (c.is_chunked) {
        if (!decode_chunked(c.body_raw, req.body)) {
        err = "Chunked decode failed (request)";
        return false;
        }
    } else if (c.content_length) {
        if (c.body_raw.size() < *c.content_length) {
        err = "Body shorter than Content-Length (request)";
        return false;
        }
        req.body = c.body_raw.substr(0, *c.content_length);
    } else {
        // no length -> take all remaining
        req.body = std::move(c.body_raw);
    }

    return true;
}

bool Http::parse_response(const std::string& raw, Response& res, std::string& err) {
    ParseCommonResult c;
    if (!parse_common(raw, c, err)) return false;

    // Start-line: HTTP/VERSION SP STATUS SP REASON
    // Example: HTTP/1.1 200 OK
    auto a = c.start_line.find(' ');
    auto b = (a == std::string::npos) ? std::string::npos : c.start_line.find(' ', a + 1);
    if (a == std::string::npos) {
        err = "Invalid response start-line";
        return false;
    }

    res = Response{};
    res.version = c.start_line.substr(0, a);
    trim_inplace(res.version);

    std::string status_str;
    if (b == std::string::npos) {
        status_str = c.start_line.substr(a + 1);
        res.reason.clear();
    } else {
        status_str = c.start_line.substr(a + 1, b - (a + 1));
        res.reason = c.start_line.substr(b + 1);
        trim_inplace(res.reason);
    }
    trim_inplace(status_str);
    res.status_code = std::atoi(status_str.c_str());

    res.headers = std::move(c.headers);

    // Body finalize
    if (c.is_chunked) {
        if (!decode_chunked(c.body_raw, res.body)) {
        err = "Chunked decode failed (response)";
        return false;
        }
    } else if (c.content_length) {
        if (c.body_raw.size() < *c.content_length) {
        err = "Body shorter than Content-Length (response)";
        return false;
        }
        res.body = c.body_raw.substr(0, *c.content_length);
    } else {
        res.body = std::move(c.body_raw);
    }

    return true;
}
