#ifndef __HTTPCODES_H__
#define __HTTPCODES_H__

#include <string>
#include <sstream>

enum class HttpMethod {
    GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS, CONNECT, END
};

enum class HttpCode {
    Continue = 100
    , Switching_Protocol
    , Processiong
    , Early_Hints
    , OK = 200
    , Created
    , Accepted
    , Non_Authoritative_Information
    , No_Content
    , Reset_Content
    , Partial_Content
    , Multi_Status
    , Already_Reported
    , IM_Used = 226
    , Multiple_Choice = 300
    , Moved_Permanently
    , Found
    , See_Other
    , Not_Modified
    , Use_Proxy
    , unused
    , Temporary_Redirect
    , Permanent_Redirect
    , Bad_Request = 400
    , Unauthorized
    , Payment_Required
    , Forbidden
    , Not_Found
    , Method_Not_Allowed
    , Not_Acceptable
    , Proxy_Authentication_Required
    , Request_Timeout
    , Conflict
    , Gone
    , Internal_Server_Error = 500
    , Not_Implemented
    , Bad_Gateway
    , Service_Unavailable
    , Gateway_Timeout
    , END
};

/**
 * @brief Get the Method Str message
 * 
 * @param method 
 * @return std::string 
 */
std::string getMethodStr(HttpMethod method);

/**
 * @brief Get the Code Str message
 * 
 * @param code 
 * @return std::string 
 */
std::string getCodeStr(HttpCode code);

/**
 * @brief Get numeric HTTP status code value.
 *
 * @param code
 * @return int
 */
int getCodeValue(HttpCode code);

/**
 * @brief Get reason phrase only (e.g. "OK", "Created") from HttpCode.
 *
 * @param code
 * @return std::string
 */
std::string getCodeReason(HttpCode code);

#endif // __HTTPCODES_H__
