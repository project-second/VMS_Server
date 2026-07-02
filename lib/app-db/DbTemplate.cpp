#include "DbTemplate.hpp"

#include <filesystem>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <utility>
#include <type_traits>

#include <yaml-cpp/yaml.h>

namespace {
constexpr unsigned int kOidBool = 16;
constexpr unsigned int kOidInt2 = 21;
constexpr unsigned int kOidInt4 = 23;
constexpr unsigned int kOidInt8 = 20;
constexpr unsigned int kOidFloat4 = 700;
constexpr unsigned int kOidFloat8 = 701;
constexpr unsigned int kOidNumeric = 1700;
}

DbTemplate::DbTemplate(std::string conninfo, std::string mapperPath)
    : mapperPath_(std::move(mapperPath)),
      cx_(std::move(conninfo)) {
    if (!cx_.is_open()) {
        throw std::runtime_error("failed to open PostgreSQL connection");
    }

    {
        pqxx::work tx(cx_);
        const pqxx::row row = tx.exec1(
            "SELECT "
            "current_database(), "
            "current_user, "
            "current_schema(), "
            "current_setting('search_path'), "
            "COALESCE(inet_server_addr()::text, 'local'), "
            "inet_server_port()");
        tx.commit();
        std::cout
            << "[DB] session db=" << row[0].c_str()
            << " user=" << row[1].c_str()
            << " schema=" << row[2].c_str()
            << " search_path=" << row[3].c_str()
            << " server=" << row[4].c_str()
            << ":" << row[5].as<int>()
            << '\n';
    }

    loadMapperIndex();
}

DbTemplate::~DbTemplate() = default;

void DbTemplate::insert(const std::string& statement, const ParamMap& params) {
    executeMutation(statement, "insert", params);
}

void DbTemplate::update(const std::string& statement, const ParamMap& params) {
    executeMutation(statement, "update", params);
}

DbTemplate::TypedRowSet DbTemplate::selectTyped(const std::string& statement) {
    return selectTyped(statement, ParamMap{});
}

DbTemplate::TypedRowSet DbTemplate::selectTyped(const std::string& statement, const ParamMap& params) {
    const std::string query = getQuery(statement, "select");
    const PreparedSql prepared = toPrepared(query);

    pqxx::work tx(cx_);
    pqxx::result result = tx.exec_params(prepared.sql, buildParams(prepared.orderedParams, params));
    tx.commit();

    return toTypedRowSet(result);
}

DbTemplate::TypedRowSet DbTemplate::selectTypedWithAdvisoryLock(
    std::int64_t lockId,
    const std::string& statement,
    const ParamMap& params) {
    const std::string query = getQuery(statement, "select");
    const PreparedSql prepared = toPrepared(query);

    pqxx::work tx(cx_);
    pqxx::params lockParams;
    lockParams.append(lockId);
    tx.exec_params("SELECT pg_advisory_xact_lock($1)", lockParams);
    pqxx::result result = tx.exec_params(prepared.sql, buildParams(prepared.orderedParams, params));
    tx.commit();

    return toTypedRowSet(result);
}

void DbTemplate::deleteQ(const std::string& statement, const ParamMap& params) {
    executeMutation(statement, "delete", params);
}

void DbTemplate::loadMapperIndex() {
    namespace fs = std::filesystem;

    mapperFileName_.clear();

    if (!fs::exists(mapperPath_) || !fs::is_directory(mapperPath_)) {
        throw std::runtime_error("mapper directory not found: " + mapperPath_);
    }

    for (const auto& entry : fs::directory_iterator(mapperPath_)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const fs::path ext = entry.path().extension();
        if (ext != ".yml" && ext != ".yaml") {
            continue;
        }

        YAML::Node config = YAML::LoadFile(entry.path().string());
        const YAML::Node mapper = config["mapper"];
        if (!mapper || !mapper["namespace"]) {
            continue;
        }

        const std::string ns = mapper["namespace"].as<std::string>();
        mapperFileName_[ns] = entry.path().string();
    }
}

std::pair<std::string, std::string> DbTemplate::splitStatement(const std::string& statement) {
    const std::size_t pos = statement.find('.');
    if (pos == std::string::npos || pos == 0 || pos == statement.size() - 1) {
        throw std::runtime_error("invalid statement format (expected namespace.id): " + statement);
    }

    return {statement.substr(0, pos), statement.substr(pos + 1)};
}

std::string DbTemplate::getQuery(const std::string& statement, const std::string& sequence) const {
    const auto [ns, id] = splitStatement(statement);

    const auto it = mapperFileName_.find(ns);
    if (it == mapperFileName_.end()) {
        throw std::runtime_error("mapper namespace not found: " + ns);
    }

    YAML::Node config = YAML::LoadFile(it->second);
    const YAML::Node objects = config["mapper"]["objects"];
    if (!objects || !objects.IsSequence()) {
        throw std::runtime_error("invalid mapper file (objects missing): " + it->second);
    }

    for (const auto& item : objects) {
        if (!item["id"] || !item["sequence"] || !item["query"]) {
            continue;
        }

        const std::string itemId = item["id"].as<std::string>();
        const std::string itemSeq = item["sequence"].as<std::string>();
        if (itemId == id && itemSeq == sequence) {
            return item["query"].as<std::string>();
        }
    }

    throw std::runtime_error("query not found for statement: " + statement + ", sequence: " + sequence);
}

DbTemplate::PreparedSql DbTemplate::toPrepared(const std::string& tmpl) {
    static const std::regex rx(R"(#\{([A-Za-z_]\w*)\})");

    PreparedSql out;
    out.sql.reserve(tmpl.size());

    std::size_t cursor = 0;
    int index = 1;

    for (std::sregex_iterator it(tmpl.begin(), tmpl.end(), rx), end; it != end; ++it, ++index) {
        const std::smatch& m = *it;
        out.sql.append(tmpl, cursor, static_cast<std::size_t>(m.position()) - cursor);
        out.sql += "$" + std::to_string(index);
        out.orderedParams.emplace_back(m[1].str());
        cursor = static_cast<std::size_t>(m.position() + m.length());
    }

    out.sql.append(tmpl, cursor, std::string::npos);
    return out;
}

pqxx::params DbTemplate::buildParams(const std::vector<std::string>& orderedParams, const ParamMap& params) {
    pqxx::params out;

    for (const auto& name : orderedParams) {
        const auto it = params.find(name);
        if (it == params.end()) {
            throw std::runtime_error("missing SQL parameter: " + name);
        }

        std::visit(
            [&out](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    out.append(static_cast<const char*>(nullptr));
                } else {
                    out.append(value);
                }
            },
            it->second);
    }

    return out;
}

DbTemplate::TypedRowSet DbTemplate::toTypedRowSet(const pqxx::result& result) {
    TypedRowSet rows;
    rows.reserve(result.size());

    for (const auto& row : result) {
        TypedRow map;
        map.reserve(row.size());

        for (const auto& field : row) {
            if (field.is_null()) {
                map[field.name()] = std::monostate{};
                continue;
            }

            try {
                switch (field.type()) {
                    case kOidBool:
                        map[field.name()] = field.as<bool>();
                        break;
                    case kOidInt2:
                    case kOidInt4:
                    case kOidInt8:
                        map[field.name()] = field.as<std::int64_t>();
                        break;
                    case kOidFloat4:
                    case kOidFloat8:
                    case kOidNumeric:
                        map[field.name()] = field.as<double>();
                        break;
                    default:
                        map[field.name()] = std::string(field.c_str());
                        break;
                }
            } catch (const std::exception&) {
                map[field.name()] = std::string(field.c_str());
            }
        }

        rows.emplace_back(std::move(map));
    }

    return rows;
}

void DbTemplate::executeMutation(const std::string& statement,
                                 const std::string& sequence,
                                 const ParamMap& params) {
    const std::string query = getQuery(statement, sequence);
    const PreparedSql prepared = toPrepared(query);

    pqxx::work tx(cx_);
    tx.exec_params(prepared.sql, buildParams(prepared.orderedParams, params));
    tx.commit();
}
