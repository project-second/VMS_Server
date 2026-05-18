#include "Digest.h"

// ------------------------------
// Digest 파싱
// ------------------------------

static std::map<std::string, std::string> parse_digest_kv(const std::string& header_value) {
    // 예:
    // Digest algorithm="MD5", qop="auth", realm="xxx", nonce="yyy"
    std::string s = trim(header_value);

    if (starts_with_case_insensitive(s, "Digest")) {
        s = trim(s.substr(6));
    }

    std::map<std::string, std::string> kv;
    std::size_t i = 0;

    while (i < s.size()) {
        while (i < s.size() && (s[i] == ',' || std::isspace(static_cast<unsigned char>(s[i])))) {
            ++i;
        }
        if (i >= s.size()) {
            break;
        }

        std::size_t key_begin = i;
        while (i < s.size() && s[i] != '=' && s[i] != ',') {
            ++i;
        }
        if (i >= s.size() || s[i] != '=') {
            break;
        }

        std::string key = trim(s.substr(key_begin, i - key_begin));
        ++i; // skip '='

        std::string value;
        if (i < s.size() && s[i] == '"') {
            ++i; // skip opening quote
            std::size_t value_begin = i;
            while (i < s.size() && s[i] != '"') {
                ++i;
            }
            value = s.substr(value_begin, i - value_begin);
            if (i < s.size() && s[i] == '"') {
                ++i; // skip closing quote
            }
        } else {
            std::size_t value_begin = i;
            while (i < s.size() && s[i] != ',') {
                ++i;
            }
            value = trim(s.substr(value_begin, i - value_begin));
        }

        kv[key] = value;
    }

    return kv;
}

DigestChallenge parse_www_authenticate_digest(const std::string& www_authenticate) {
    const auto kv = parse_digest_kv(www_authenticate);

    DigestChallenge dc;

    auto it_realm = kv.find("realm");
    auto it_nonce = kv.find("nonce");
    auto it_qop = kv.find("qop");
    auto it_algorithm = kv.find("algorithm");
    auto it_opaque = kv.find("opaque");
    auto it_charset = kv.find("charset");

    if (it_realm == kv.end() || it_nonce == kv.end()) {
        throw std::runtime_error("Digest challenge missing realm or nonce");
    }

    dc.realm = it_realm->second;
    dc.nonce = it_nonce->second;
    dc.qop = (it_qop != kv.end()) ? it_qop->second : "";
    dc.algorithm = (it_algorithm != kv.end()) ? it_algorithm->second : "MD5";

    if (it_opaque != kv.end()) {
        dc.opaque = it_opaque->second;
    }
    if (it_charset != kv.end()) {
        dc.charset = it_charset->second;
    }

    return dc;
}


// ------------------------------
// Authorization 생성
// ------------------------------

std::string build_digest_authorization(const DigestChallenge& challenge
    , const DigestAuthContext& ctx) {
    if (!starts_with_case_insensitive(challenge.algorithm, "MD5")) {
        throw std::runtime_error("Only MD5/MD5-sess style digest is handled in this sample");
    }

    const std::string cnonce = random_hex(16);

    std::ostringstream nc_ss;
    nc_ss << std::hex << std::setw(8) << std::setfill('0') << ctx.nonce_count;
    const std::string nc = nc_ss.str();

    const std::string ha1 = md5_hex(ctx.username + ":" + challenge.realm + ":" + ctx.password);
    const std::string ha2 = md5_hex(ctx.method + ":" + ctx.uri);

    std::string response;
    if (!challenge.qop.empty()) {
        response = md5_hex(
            ha1 + ":" +
            challenge.nonce + ":" +
            nc + ":" +
            cnonce + ":" +
            challenge.qop + ":" +
            ha2
        );
    } else {
        response = md5_hex(ha1 + ":" + challenge.nonce + ":" + ha2);
    }

    std::ostringstream auth;
    auth << "Digest ";
    auth << "username=\"" << ctx.username << "\", ";
    auth << "realm=\"" << challenge.realm << "\", ";
    auth << "nonce=\"" << challenge.nonce << "\", ";
    auth << "uri=\"" << ctx.uri << "\", ";
    auth << "algorithm=" << challenge.algorithm << ", ";

    if (!challenge.qop.empty()) {
        auth << "qop=" << challenge.qop << ", ";
        auth << "nc=" << nc << ", ";
        auth << "cnonce=\"" << cnonce << "\", ";
    }

    auth << "response=\"" << response << "\"";

    if (challenge.opaque.has_value()) {
        auth << ", opaque=\"" << *challenge.opaque << "\"";
    }

    return auth.str();
}