#ifndef __DBTEMPLATE_H__
#define __DBTEMPLATE_H__

#include <string>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <pqxx/pqxx>

/**
 * @brief Mapper 기반 SQL 실행을 제공하는 DB 템플릿 클래스.
 *
 * 매퍼 파일에서 statement 식별자에 해당하는 SQL 템플릿을 조회하고,
 * 파라미터 바인딩 후 PostgreSQL에 쿼리를 실행한다.
 */
class DbTemplate {
public:
    /** @brief SQL 파라미터 스칼라 값 타입. */
    using ParamValue = std::variant<std::monostate, bool, std::int64_t, double, std::string>;
    /** @brief 조회 결과 셀 값 타입. */
    using CellValue = std::variant<std::monostate, bool, std::int64_t, double, std::string>;
    /** @brief SQL 템플릿 파라미터 맵 (`name -> value`). */
    using ParamMap = std::unordered_map<std::string, ParamValue>;
    /** @brief 타입 보존 단일 결과 행 (`column -> typed value`). */
    using TypedRow = std::unordered_map<std::string, CellValue>;
    /** @brief 타입 보존 다중 결과 행 집합. */
    using TypedRowSet = std::vector<TypedRow>;

private:
    struct PreparedSql {
        std::string sql;
        std::vector<std::string> orderedParams;
    };

    std::string mapperPath_;
    std::unordered_map<std::string, std::string> mapperFileName_;
    pqxx::connection cx_;

public:
    /**
     * @brief DbTemplate 인스턴스를 생성한다.
     * @param conninfo PostgreSQL 연결 문자열.
     * @param mapperPath SQL mapper 파일들이 위치한 디렉터리 경로.
     */
    DbTemplate(std::string conninfo, std::string mapperPath);
    /** @brief 복사 생성은 지원하지 않는다. */
    DbTemplate(const DbTemplate&) = delete;
    /** @brief 이동 생성은 지원하지 않는다. */
    DbTemplate(DbTemplate&&) = delete;
    /** @brief 리소스를 정리한다. */
    ~DbTemplate();

    /** @brief 복사 대입은 지원하지 않는다. */
    DbTemplate& operator=(const DbTemplate&) = delete;
    /** @brief 이동 대입은 지원하지 않는다. */
    DbTemplate& operator=(DbTemplate&&) = delete;

    /**
     * @brief INSERT 문을 실행한다.
     * @param statement 실행할 statement 식별자(`mapperId.statementId`).
     * @param params SQL 템플릿 파라미터 값.
     */
    void insert(const std::string& statement, const ParamMap& params);

    /**
     * @brief UPDATE 문을 실행한다.
     * @param statement 실행할 statement 식별자(`mapperId.statementId`).
     * @param params SQL 템플릿 파라미터 값.
     */
    void update(const std::string& statement, const ParamMap& params);

    /**
     * @brief 파라미터 없는 SELECT 문을 타입 보존 형태로 실행한다.
     * @param statement 실행할 statement 식별자(`mapperId.statementId`).
     * @return 타입 보존 조회 결과 행 집합.
     */
    TypedRowSet selectTyped(const std::string& statement);

    /**
     * @brief 파라미터를 바인딩하여 SELECT 문을 타입 보존 형태로 실행한다.
     * @param statement 실행할 statement 식별자(`mapperId.statementId`).
     * @param params SQL 템플릿 파라미터 값.
     * @return 타입 보존 조회 결과 행 집합.
     */
    TypedRowSet selectTyped(const std::string& statement, const ParamMap& params);

    /**
     * @brief DELETE 문을 실행한다.
     * @param statement 실행할 statement 식별자(`mapperId.statementId`).
     * @param params SQL 템플릿 파라미터 값.
     */
    void deleteQ(const std::string& statement, const ParamMap& params);

private:
    void loadMapperIndex();

    static std::pair<std::string, std::string> splitStatement(const std::string& statement);
    std::string getQuery(const std::string& statement, const std::string& sequence) const;

    static PreparedSql toPrepared(const std::string& tmpl);
    static pqxx::params buildParams(const std::vector<std::string>& orderedParams, const ParamMap& params);
    static TypedRowSet toTypedRowSet(const pqxx::result& result);

    void executeMutation(const std::string& statement, const std::string& sequence, const ParamMap& params);
};

#endif // __DBTEMPLATE_H__
