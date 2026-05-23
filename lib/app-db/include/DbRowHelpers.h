#ifndef __DBROWHELPERS_H__
#define __DBROWHELPERS_H__

#include "DbTemplate.hpp"
#include "StringHelpers.h"

#include <cstdint>
#include <string>

namespace DbRow {

inline bool getStringCell(const DbTemplate::TypedRow& row, const std::string& key, std::string& out) {
    const auto it = row.find(key);
    if (it == row.end() || std::holds_alternative<std::monostate>(it->second)) return false;

    if (const auto* value = std::get_if<std::string>(&it->second)) {
        out = *value;
        return true;
    }
    if (const auto* value = std::get_if<std::int64_t>(&it->second)) {
        out = std::to_string(*value);
        return true;
    }
    if (const auto* value = std::get_if<bool>(&it->second)) {
        out = *value ? "true" : "false";
        return true;
    }
    if (const auto* value = std::get_if<double>(&it->second)) {
        out = std::to_string(*value);
        return true;
    }
    return false;
}

inline bool getInt64Cell(const DbTemplate::TypedRow& row, const std::string& key, std::int64_t& out) {
    const auto it = row.find(key);
    if (it == row.end() || std::holds_alternative<std::monostate>(it->second)) return false;

    if (const auto* value = std::get_if<std::int64_t>(&it->second)) {
        out = *value;
        return true;
    }
    if (const auto* value = std::get_if<std::string>(&it->second)) {
        return parse_int64_strict(*value, out);
    }
    return false;
}

inline bool getBoolCell(const DbTemplate::TypedRow& row, const std::string& key, bool& out) {
    const auto it = row.find(key);
    if (it == row.end() || std::holds_alternative<std::monostate>(it->second)) return false;

    if (const auto* value = std::get_if<bool>(&it->second)) {
        out = *value;
        return true;
    }
    if (const auto* value = std::get_if<std::string>(&it->second)) {
        const std::string normalized = to_lower(trim(*value));
        if (normalized == "true" || normalized == "t" || normalized == "1") {
            out = true;
            return true;
        }
        if (normalized == "false" || normalized == "f" || normalized == "0") {
            out = false;
            return true;
        }
    }
    return false;
}

} // namespace DbRow

#endif // __DBROWHELPERS_H__
