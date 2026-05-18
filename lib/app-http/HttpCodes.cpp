#include "HttpCodes.h"

typedef struct method_type {
    HttpMethod code;
    std::string codeStr;
} METHOD_TYPE;

METHOD_TYPE methods[] {
    HttpMethod::GET,        "GET",
    HttpMethod::POST,       "POST",
    HttpMethod::PUT,        "PUT",
    HttpMethod::DELETE,     "DELETE",
    HttpMethod::PATCH,      "PATCH",
    HttpMethod::HEAD,       "HEAD",
    HttpMethod::OPTIONS,    "OPTIONS",
    HttpMethod::CONNECT,    "CONNECT",
    HttpMethod::END,        "END"
};

typedef struct code_type {
    HttpCode code;
    std::string codeStr;
} CODE_TYPE;

CODE_TYPE codes[] {
    HttpCode::Continue,                         "100 Continue",
    HttpCode::Switching_Protocol,               "101 Switching Protocol",
    HttpCode::Processiong,                      "102 Processing",
    HttpCode::Early_Hints,                      "103 Early Hints",
    HttpCode::OK,                               "200 OK",
    HttpCode::Created,                          "201 Created",
    HttpCode::Accepted,                         "202 Accepted",
    HttpCode::Non_Authoritative_Information,    "203 Non-Authoritative Information",
    HttpCode::No_Content,                       "204 No Content",
    HttpCode::Reset_Content,                    "205 Reset Content",
    HttpCode::Partial_Content,                  "206 Partial Content",
    HttpCode::Multi_Status,                     "207 Multi-Status",
    HttpCode::Already_Reported,                 "208 Already Reported",
    HttpCode::IM_Used,                          "226 IM Used",
    HttpCode::Multiple_Choice,                  "300 Multiple Choice",
    HttpCode::Moved_Permanently,                "301 Moved Permanently",
    HttpCode::Found,                            "302 Found",
    HttpCode::See_Other,                        "303 See Other",
    HttpCode::Not_Modified,                     "304 Not Modified",
    HttpCode::Use_Proxy,                        "305 Use Proxy",
    HttpCode::unused,                           "306 unused",
    HttpCode::Temporary_Redirect,               "307 Temporary Redirect",
    HttpCode::Permanent_Redirect,               "308 Permanent Redirect",
    HttpCode::Bad_Request,                      "400 Bad Request",
    HttpCode::Unauthorized,                     "401 Unauthorized",
    HttpCode::Payment_Required,                 "402 Payment Required",
    HttpCode::Forbidden,                        "403 Forbidden",
    HttpCode::Not_Found,                        "404 Not Found",
    HttpCode::Method_Not_Allowed,               "405 Method Not Allowed",
    HttpCode::Not_Acceptable,                   "406 Not Acceptable",
    HttpCode::Proxy_Authentication_Required,    "407 Proxy Authentication Required",
    HttpCode::Request_Timeout,                  "408 Request Timeout",
    HttpCode::Conflict,                         "409 Conflict",
    HttpCode::Gone,                             "410 Gone",
    HttpCode::Internal_Server_Error,            "500 Internal Server Error",
    HttpCode::Not_Implemented,                  "501 Not Implemented",
    HttpCode::Bad_Gateway,                      "502 Bad Gateway",
    HttpCode::Service_Unavailable,              "503 Service Unavailable",
    HttpCode::Gateway_Timeout,                  "504 Gateway Timeout",
    HttpCode::END,                              "END"
};

std::string getMethodStr(HttpMethod method) {
    for (int i = 0; methods[i].code != HttpMethod::END; i++) {
        if (methods[i].code == method) { return methods[i].codeStr; }
    }
    return "END";
}

std::string getCodeStr(HttpCode code) {
    for (int i = 0; codes[i].code != HttpCode::END; i++) {
        if (codes[i].code == code) { return codes[i].codeStr; }
    }
    return "END";
}

int getCodeValue(HttpCode code) {
    return static_cast<int>(code);
}

std::string getCodeReason(HttpCode code) {
    const std::string status = getCodeStr(code);
    if (status == "END") return "Unknown";

    const std::size_t first_space = status.find(' ');
    if (first_space == std::string::npos) return status;

    return status.substr(first_space + 1);
}
