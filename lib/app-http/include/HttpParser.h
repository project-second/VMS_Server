#ifndef __HTTPPARSER_H__
#define __HTTPPARSER_H__

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <unordered_map>

#include "HttpCodes.h"
#include "StringHelpers.h"

namespace Http {

struct ParsedUrl {
    std::string scheme; // "http" or "https"
    std::string host;   // ip or hostname
    uint16_t    port;   // resolved port
    std::string path;   // starts with '/', may be empty -> "/"
};

// Header container (case-insensitive lookup by lower-case keys)
struct Headers {
    std::unordered_map<std::string, std::string> kv; // key: lower-case

    void set(std::string key, std::string value) {
        key = to_lower(std::move(key));
        trim_inplace(value);
        kv[std::move(key)] = std::move(value);
    }

    const std::string* get(const std::string& key) const {
        auto it = kv.find(to_lower(key));
        if (it == kv.end()) return nullptr;
        return &it->second;
    }

    bool has(const std::string& key) const { return get(key) != nullptr; }
};

// HTTP Request
struct Request {
    std::string method;
    std::string target;   // path or absolute-form
    std::string version;  // "HTTP/1.1"
    Headers headers;
    std::string body;
    std::unordered_map<std::string, std::string> path_params;
    std::unordered_map<std::string, std::string> query_params;

    const std::string* getPathParam(const std::string& key) const {
        auto it = path_params.find(key);
        if (it == path_params.end()) return nullptr;
        return &it->second;
    }

    const std::string* getQueryParam(const std::string& key) const {
        auto it = query_params.find(key);
        if (it == query_params.end()) return nullptr;
        return &it->second;
    }
};

// HTTP Response
struct Response {
    std::string version;  // "HTTP/1.1"
    int status_code = 0;
    std::string reason;
    Headers headers;
    std::string body;

    void setStatus(HttpCode code) {
        status_code = getCodeValue(code);
        reason = getCodeReason(code);
    }
};

/**
 * @brief http url 파서
 * 
 * @param url  https://example.com:8443/path/to/api 와 같은 url 입력
 * @return std::optional<ParsedUrl> 
 */
std::optional<ParsedUrl> parse_http_https_url(std::string url);

bool parse_request(const std::string& raw, Request& req, std::string& err);

bool parse_response(const std::string& raw, Response& res, std::string& err);

}

#endif // __HTTPPARSER_H__
