#include "SecretCodec.h"

#include "StringHelpers.h"

std::optional<std::string> decode_stored_secret(const std::string& secret) {
    const std::string value = trim(secret);
    if (value.empty()) {
        return std::nullopt;
    }

    constexpr const char* kPlainPrefix = "plain:";
    if (value.rfind(kPlainPrefix, 0) == 0) {
        return value.substr(6);
    }

    return value;
}

std::string encode_plain_secret(const std::string& secret) {
    return "plain:" + secret;
}
