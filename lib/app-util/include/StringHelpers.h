#ifndef __STRINGHELPERS_H__
#define __STRINGHELPERS_H__

#include <string>
#include <vector>
#include <random>
#include <cstdint>

void trim_inplace(std::string& s);

std::string trim(const std::string& s);

std::string to_lower(std::string s);

std::vector<std::string> split_once(const std::string& s, char delim);

bool starts_with_case_insensitive(const std::string& s, const std::string& prefix);

bool equals_case_insensitive(const std::string& lhs, const std::string& rhs);

bool is_hex_string(const std::string& s);
bool hex_char_to_int(char ch, int& out);
std::string url_decode(const std::string& raw, bool plus_as_space = true);
bool is_valid_date_ymd(const std::string& date);
bool is_valid_rfc3339_ts(const std::string& ts);

std::string utc_now_iso8601();

bool parse_int64_strict(const std::string& raw, std::int64_t& out);

std::string random_hex(std::size_t len);

#endif // __STRINGHELPERS_H__
