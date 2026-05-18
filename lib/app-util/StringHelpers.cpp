#include "StringHelpers.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <regex>
#include <sstream>

static inline void ltrim_inplace(std::string& s) {
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    s.erase(0, i);
}

static inline void rtrim_inplace(std::string& s) {
    size_t i = s.size();
    while (i > 0 && std::isspace(static_cast<unsigned char>(s[i - 1]))) --i;
    s.erase(i);
}

void trim_inplace(std::string& s) { ltrim_inplace(s); rtrim_inplace(s); }

std::string trim(const std::string& s) {
    std::size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }

    std::size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }

    return s.substr(begin, end - begin);
}

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> split_once(const std::string& s, char delim) {
    auto pos = s.find(delim);
    if (pos == std::string::npos) return {s};
    return {s.substr(0, pos), s.substr(pos + 1)};
}

bool starts_with_case_insensitive(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) {
        return false;
    }

    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i]))) {
            return false;
        }
    }
    return true;
}

bool equals_case_insensitive(const std::string& lhs, const std::string& rhs) {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(lhs[i])) !=
            std::tolower(static_cast<unsigned char>(rhs[i]))) {
            return false;
        }
    }
    return true;
}

bool is_hex_string(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char ch : s) {
        const bool is_digit = (ch >= '0' && ch <= '9');
        const bool is_lower_hex = (ch >= 'a' && ch <= 'f');
        const bool is_upper_hex = (ch >= 'A' && ch <= 'F');
        if (!is_digit && !is_lower_hex && !is_upper_hex) {
            return false;
        }
    }
    return true;
}

bool hex_char_to_int(char ch, int& out) {
    if (ch >= '0' && ch <= '9') {
        out = ch - '0';
        return true;
    }
    if (ch >= 'a' && ch <= 'f') {
        out = 10 + (ch - 'a');
        return true;
    }
    if (ch >= 'A' && ch <= 'F') {
        out = 10 + (ch - 'A');
        return true;
    }
    return false;
}

std::string url_decode(const std::string& raw, bool plus_as_space) {
    std::string out;
    out.reserve(raw.size());

    for (std::size_t i = 0; i < raw.size(); ++i) {
        const char ch = raw[i];
        if (ch == '%' && i + 2 < raw.size()) {
            int hi = 0;
            int lo = 0;
            if (hex_char_to_int(raw[i + 1], hi) && hex_char_to_int(raw[i + 2], lo)) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        if (plus_as_space && ch == '+') {
            out.push_back(' ');
            continue;
        }
        out.push_back(ch);
    }

    return out;
}

bool is_valid_date_ymd(const std::string& date) {
    static const std::regex kYmdPattern(R"(^(\d{4})-(\d{2})-(\d{2})$)");
    std::smatch m;
    if (!std::regex_match(date, m, kYmdPattern)) {
        return false;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    try {
        year = std::stoi(m[1].str());
        month = std::stoi(m[2].str());
        day = std::stoi(m[3].str());
    } catch (...) {
        return false;
    }

    if (year < 1 || month < 1 || month > 12 || day < 1) {
        return false;
    }

    static const int kDaysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int max_day = kDaysInMonth[month - 1];
    const bool leap_year = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    if (month == 2 && leap_year) {
        max_day = 29;
    }
    return day <= max_day;
}

bool is_valid_rfc3339_ts(const std::string& ts) {
    static const std::regex kTsPattern(
        R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d{1,6})?(Z|[+-]\d{2}:\d{2})$)");
    return std::regex_match(ts, kTsPattern);
}

std::string utc_now_iso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &now_time);
#else
    gmtime_r(&now_time, &tm_utc);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

bool parse_int64_strict(const std::string& raw, std::int64_t& out) {
    if (raw.empty()) return false;
    std::size_t pos = 0;
    try {
        out = std::stoll(raw, &pos, 10);
    } catch (...) {
        return false;
    }
    return pos == raw.size();
}

std::string random_hex(std::size_t len) {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    static constexpr char kHex[] = "0123456789abcdef";

    std::string out;
    out.reserve(len);

    std::uniform_int_distribution<int> dist(0, 15);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(kHex[dist(rng)]);
    }

    return out;
}
