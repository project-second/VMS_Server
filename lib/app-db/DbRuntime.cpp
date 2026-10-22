/**
 * @file DbRuntime.cpp
 * @brief 공유 DB 인스턴스의 설정 저장, 지연 초기화 및 SQL 실행 오류 처리 구현.
 */
#include "DbRuntime.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <utility>


namespace AppDb {
namespace {

/**
 * @brief 상대 매퍼 경로를 현재 작업 디렉터리 기준으로 바꾸고 경로의 . 및 .. 요소를 정리한다.
 * @param raw_path 설정에서 전달받은 SQL 매퍼 디렉터리 경로.
 * @return 경로 요소를 정리한 절대 경로 문자열.
 * @throws std::filesystem::filesystem_error 현재 작업 디렉터리를 조회할 수 없는 경우.
 */
std::string resolve_mapper_path(const std::string& raw_path) {
    namespace fs = std::filesystem;
    fs::path path(raw_path);
    if (path.is_relative()) {
        path = fs::current_path() / path;
    }
    return path.lexically_normal().string();
}

/**
 * @brief AppDb 실행 함수들의 공유 DB 연결 사용을 직렬화하는 뮤텍스를 반환한다.
 * @return 프로세스 내에서 공유하는 DB 실행 뮤텍스.
 */
std::mutex& db_exec_mutex() {
    static std::mutex m;
    return m;
}

/**
 * @brief 이 모듈의 로그 출력이 서로 섞이지 않도록 보호하는 뮤텍스를 반환한다.
 * @return DB 런타임 로그 함수들이 공유하는 출력 뮤텍스.
 */
std::mutex& db_log_mutex() {
    static std::mutex m;
    return m;
}

/**
 * @brief 공유 DbTemplate의 최초 초기화에 사용할 연결 정보와 매퍼 경로.
 */
struct DbSettings {
    std::string conninfo; ///< PostgreSQL 연결 문자열.
    std::string mapperPath; ///< SQL 매퍼 디렉터리 경로.
    bool configured = false; ///< configure() 호출로 설정이 등록되었는지 여부.
};

/**
 * @brief DB 초기화에 사용할 공유 설정 저장소를 반환한다.
 * @pre 설정에 접근하는 동안 db_settings_mutex()를 잠가야 한다.
 * @return 프로세스 공통 DB 설정에 대한 참조.
 */
DbSettings& db_settings() {
    static DbSettings settings;
    return settings;
}

/**
 * @brief DB 설정 저장과 초기화용 설정 복사를 보호하는 뮤텍스를 반환한다.
 * @return 공유 DB 설정 접근용 뮤텍스.
 */
std::mutex& db_settings_mutex() {
    static std::mutex m;
    return m;
}

/**
 * @brief 인자를 순서대로 이어 붙여 DB 정보 로그 한 줄을 표준 출력에 기록한다.
 * @tparam Args 출력 스트림에 삽입할 수 있는 인자 타입들.
 * @param args 로그에 순서대로 출력할 값들.
 */
template <typename... Args>
void log_info(Args&&... args) {
    std::lock_guard<std::mutex> lock(db_log_mutex());
    (std::cout << ... << std::forward<Args>(args)) << '\n';
}

/**
 * @brief 인자를 순서대로 이어 붙여 DB 오류 로그 한 줄을 표준 오류에 기록한다.
 * @tparam Args 출력 스트림에 삽입할 수 있는 인자 타입들.
 * @param args 로그에 순서대로 출력할 값들.
 */
template <typename... Args>
void log_error(Args&&... args) {
    std::lock_guard<std::mutex> lock(db_log_mutex());
    (std::cerr << ... << std::forward<Args>(args)) << '\n';
}

} // namespace

/**
 * @brief 설정 뮤텍스 안에서 DB 연결 정보와 매퍼 경로를 저장하고 등록 상태를 표시한다.
 * @param conninfo PostgreSQL 연결 문자열.
 * @param mapper_path 최초 DB 초기화에 사용할 SQL 매퍼 디렉터리 경로.
 */
void configure(const std::string& conninfo, const std::string& mapper_path) {
    std::lock_guard<std::mutex> lock(db_settings_mutex());
    DbSettings& settings = db_settings();
    settings.conninfo = conninfo;
    settings.mapperPath = mapper_path;
    settings.configured = true;
}

/**
 * @brief 최초 호출에서 공유 DbTemplate을 초기화하고 저장된 결과를 반환한다.
 * @return 내부에서 소유하는 DB 인스턴스의 비소유 포인터. 설정 누락 또는 생성 실패 처리 시 nullptr.
 * @throws std::filesystem::filesystem_error 매퍼 경로 보정 중 파일 시스템 오류가 발생한 경우.
 */
DbTemplate* instance() {
    // nullptr로 초기화가 완료된 경우도 static에 보관해 이후 호출에서 그대로 반환한다.
    static std::unique_ptr<DbTemplate> db = []() -> std::unique_ptr<DbTemplate> {
        DbSettings settings;
        // 공유 설정을 잠금 안에서 복사하고, DB 연결과 매퍼 로딩은 잠금 밖에서 수행한다.
        {
            std::lock_guard<std::mutex> lock(db_settings_mutex());
            settings = db_settings();
        }

        if (!settings.configured || settings.conninfo.empty() || settings.mapperPath.empty()) {
            log_error("[DB] disabled: database config is not configured.");
            return nullptr;
        }

        // 경로 보정은 try 밖에서 수행하므로 파일 시스템 예외는 호출자에게 전달된다.
        const std::string mapper_path = resolve_mapper_path(settings.mapperPath);
        try {
            auto created = std::make_unique<DbTemplate>(settings.conninfo, mapper_path);
            log_info("[DB] initialized. mapper_path=", mapper_path);
            return created;
        } catch (const std::exception& e) {
            // 생성 중 발생한 표준 예외는 로그로 남기고 DB 비활성화 상태로 초기화를 마친다.
            log_error("[DB] disabled: initialization failed. reason=", e.what());
            return nullptr;
        }
    }();
    return db.get();
}

/**
 * @brief 공유 DB 접근을 직렬화해 INSERT를 실행하고 실행 예외를 실패 값으로 처리한다.
 * @param statement 실행할 매퍼 구문 식별자.
 * @param params SQL 템플릿에 바인딩할 파라미터 값.
 * @return 실행에 성공하면 true, DB 비활성화 또는 실행 예외 처리 시 false.
 */
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

/**
 * @brief 공유 DB 접근을 직렬화해 UPDATE를 실행하고 실행 예외를 실패 값으로 처리한다.
 * @param statement 실행할 매퍼 구문 식별자.
 * @param params SQL 템플릿에 바인딩할 파라미터 값.
 * @return 실행에 성공하면 true, DB 비활성화 또는 실행 예외 처리 시 false.
 */
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

/**
 * @brief 공유 DB 접근을 직렬화해 DELETE를 실행하고 실행 예외를 실패 값으로 처리한다.
 * @param statement 실행할 매퍼 구문 식별자.
 * @param params SQL 템플릿에 바인딩할 파라미터 값.
 * @return 실행에 성공하면 true, DB 비활성화 또는 실행 예외 처리 시 false.
 */
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

/**
 * @brief 공유 DB 접근을 직렬화해 SELECT를 실행하고 셀 타입을 보존한 결과를 반환한다.
 * @param statement 실행할 매퍼 구문 식별자.
 * @param params SQL 템플릿에 바인딩할 파라미터 값.
 * @return 조회된 행 목록. 조회 결과가 없거나 DB 비활성화·실행 예외 처리 시 빈 목록.
 */
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

/**
 * @brief 프로세스 내부 실행 잠금을 잡고 트랜잭션 advisory lock을 사용하는 SELECT를 위임한다.
 * @param lockId 같은 작업 간 실행을 조율할 advisory lock 식별자.
 * @param statement 실행할 매퍼 구문 식별자.
 * @param params SQL 템플릿에 바인딩할 파라미터 값.
 * @return 조회된 행 목록. 조회 결과가 없거나 DB 비활성화·실행 예외 처리 시 빈 목록.
 */
DbTemplate::TypedRowSet selectWithAdvisoryLock(
    std::int64_t lockId,
    const std::string& statement,
    const DbTemplate::ParamMap& params) {
    DbTemplate::TypedRowSet rows;
    DbTemplate* db = instance();
    if (db == nullptr) return rows;
    std::lock_guard<std::mutex> lock(db_exec_mutex());
    try {
        // PostgreSQL advisory lock의 획득과 SELECT는 DbTemplate의 한 트랜잭션에서 수행한다.
        return db->selectTypedWithAdvisoryLock(lockId, statement, params);
    } catch (const std::exception& e) {
        log_error("[DB] locked select failed. statement=", statement, " reason=", e.what());
        return rows;
    }
}

} // namespace AppDb
