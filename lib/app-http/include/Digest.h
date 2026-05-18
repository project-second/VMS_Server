#ifndef __DIGEST_H__
#define __DIGEST_H__

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "SecurityUtils.h"
#include "StringHelpers.h"

struct DigestChallenge {
    std::string realm;
    std::string nonce;
    std::string qop;
    std::string algorithm = "MD5";
    std::optional<std::string> opaque;
    std::optional<std::string> charset;
};

struct DigestAuthContext {
    std::string username;
    std::string password;
    std::string method;  // GET / POST
    std::string uri;     // path + query
    std::uint32_t nonce_count = 1;
};

DigestChallenge parse_www_authenticate_digest(const std::string& www_authenticate);

std::string build_digest_authorization(const DigestChallenge& challenge
    , const DigestAuthContext& ctx);

#endif // __DIGEST_H__