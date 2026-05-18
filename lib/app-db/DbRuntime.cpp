#include "DbRuntime.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include "EnvManager.h"

namespace AppDb {
namespace {

std::string resolve_mapper_path(const std::string& raw_path) {
    namespace fs = std::filesystem;
    fs::path path(raw_path);
    if (path.is_relative()) {
        path = fs::current_path() / path;
    }
    return path.lexically_normal().string();
}

std::mutex& db_exec_mutex() {
    static std::mutex m;
    return m;
}

std::mutex& db_log_mutex() {
    static std::mutex m;
    return m;
}

template <typename... Args>
void log_info(Args&&... args) {
    std::lock_guard<std::mutex> lock(db_log_mutex());
    (std::cout << ... << std::forward<Args>(args)) << '\n';
}

template <typename... Args>
void log_error(Args&&... args) {
    std::lock_guard<std::mutex> lock(db_log_mutex());
    (std::cerr << ... << std::forward<Args>(args)) << '\n';
}

} // namespace

DbTemplate* instance() {
    static std::unique_ptr<DbTemplate> db = []() -> std::unique_ptr<DbTemplate> {
        const auto conninfo = readEnvStringStrict("APP_DB_CONNINFO");
        const auto mapper_raw = readEnvStringStrict("APP_DB_MAPPER_PATH");
        if (!conninfo || !mapper_raw) {
            log_error("[DB] disabled: missing APP_DB_CONNINFO or APP_DB_MAPPER_PATH.");
            return nullptr;
        }
        const std::string mapper_path = resolve_mapper_path(*mapper_raw);
        try {
            auto created = std::make_unique<DbTemplate>(*conninfo, mapper_path);
            log_info("[DB] initialized. mapper_path=", mapper_path);
            return created;
        } catch (const std::exception& e) {
            log_error("[DB] disabled: initialization failed. reason=", e.what());
            return nullptr;
        }
    }();
    return db.get();
}

bool insert(const std::string& statement, const DbTemplate::ParamMap& params) {
    DbTemplate* db = instance();
    if (db == nullptr) return false;
    std::lock_guard<std::mutex> lock(db_exec_mutex());
    try {
        db->insert(statement, params);
        return true;
    } catch (const std::exception& e) {
        log_error("[DB] insert failed. statement=", statement, " reason=", e.what());
        return false;
    }
}

bool update(const std::string& statement, const DbTemplate::ParamMap& params) {
    DbTemplate* db = instance();
    if (db == nullptr) return false;
    std::lock_guard<std::mutex> lock(db_exec_mutex());
    try {
        db->update(statement, params);
        return true;
    } catch (const std::exception& e) {
        log_error("[DB] update failed. statement=", statement, " reason=", e.what());
        return false;
    }
}

bool remove(const std::string& statement, const DbTemplate::ParamMap& params) {
    DbTemplate* db = instance();
    if (db == nullptr) return false;
    std::lock_guard<std::mutex> lock(db_exec_mutex());
    try {
        db->deleteQ(statement, params);
        return true;
    } catch (const std::exception& e) {
        log_error("[DB] delete failed. statement=", statement, " reason=", e.what());
        return false;
    }
}

DbTemplate::TypedRowSet select(const std::string& statement, const DbTemplate::ParamMap& params) {
    DbTemplate::TypedRowSet rows;
    DbTemplate* db = instance();
    if (db == nullptr) return rows;
    std::lock_guard<std::mutex> lock(db_exec_mutex());
    try {
        return db->selectTyped(statement, params);
    } catch (const std::exception& e) {
        log_error("[DB] select failed. statement=", statement, " reason=", e.what());
        return rows;
    }
}

} // namespace AppDb
