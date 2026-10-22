/**
 * @file DbRuntime.hpp
 * @brief 프로세스 공통 DB 설정, 지연 초기화 및 매퍼 기반 SQL 실행 인터페이스.
 */
#ifndef __DB_RUNTIME_HPP__
#define __DB_RUNTIME_HPP__

#include <string>

#include "DbTemplate.hpp"

/**
 * @brief 하나의 공유 DbTemplate을 통해 매퍼 기반 SQL을 실행한다.
 *
 * 실행 함수들은 공통 뮤텍스로 DB 접근을 직렬화한다. 변경 함수의 bool 반환값은
 * 실행 성공 여부를 나타내며, 영향받은 행 수는 확인하지 않는다.
 */
namespace AppDb {

/**
 * @brief 공유 DB 인스턴스의 최초 초기화에 사용할 연결 정보와 매퍼 경로를 저장한다.
 *
 * instance()가 처음 호출되기 전에 설정한다. 이미 완료된 초기화 결과는 유지된다.
 * @param conninfo PostgreSQL 연결 문자열.
 * @param mapper_path SQL 매퍼 디렉터리. 상대 경로는 DB 초기화 시의 작업 디렉터리를 기준으로 한다.
 */
void configure(const std::string& conninfo, const std::string& mapper_path);

/**
 * @brief 저장된 설정으로 DB를 지연 초기화하고 공유 인스턴스를 반환한다.
 *
 * 최초 초기화가 nullptr로 완료된 경우에도 결과를 재사용한다.
 * DB 실행의 직렬화는 insert(), update(), remove(), select(), selectWithAdvisoryLock()에서 제공한다.
 * @return 내부에서 소유하는 DbTemplate의 비소유 포인터. 설정 누락 또는 인스턴스 생성 실패 처리 시 nullptr.
 * @throws std::filesystem::filesystem_error 매퍼 경로 보정 중 파일 시스템 오류가 발생한 경우.
 */
DbTemplate* instance();

/**
 * @brief 매퍼에 정의된 INSERT 문을 공유 DB에서 실행한다.
 * @param statement 실행할 매퍼 구문 식별자(`mapperId.statementId`).
 * @param params SQL 템플릿에 바인딩할 파라미터 값.
 * @return 실행에 성공하면 true, DB 비활성화 또는 SQL 실행 예외 처리 시 false.
 */
bool insert(const std::string& statement, const DbTemplate::ParamMap& params);

/**
 * @brief 매퍼에 정의된 UPDATE 문을 공유 DB에서 실행한다.
 * @param statement 실행할 매퍼 구문 식별자(`mapperId.statementId`).
 * @param params SQL 템플릿에 바인딩할 파라미터 값.
 * @return 실행에 성공하면 true, DB 비활성화 또는 SQL 실행 예외 처리 시 false.
 */
bool update(const std::string& statement, const DbTemplate::ParamMap& params);

/**
 * @brief 매퍼에 정의된 DELETE 문을 공유 DB에서 실행한다.
 * @param statement 실행할 매퍼 구문 식별자(`mapperId.statementId`).
 * @param params SQL 템플릿에 바인딩할 파라미터 값.
 * @return 실행에 성공하면 true, DB 비활성화 또는 SQL 실행 예외 처리 시 false.
 */
bool remove(const std::string& statement, const DbTemplate::ParamMap& params);

/**
 * @brief 매퍼에 정의된 SELECT 문을 실행하고 셀 타입을 보존한 결과를 반환한다.
 * @param statement 실행할 매퍼 구문 식별자(`mapperId.statementId`).
 * @param params SQL 템플릿에 바인딩할 파라미터 값.
 * @return 조회된 행 목록. 조회 결과가 없거나 DB 비활성화·SQL 실행 예외 처리 시 빈 목록.
 */
DbTemplate::TypedRowSet select(const std::string& statement, const DbTemplate::ParamMap& params);

/**
 * @brief 트랜잭션 범위의 advisory lock을 획득한 뒤 매퍼의 SELECT 문을 실행한다.
 *
 * 같은 잠금 식별자를 사용하는 트랜잭션 간 실행을 조율한다.
 * 잠금은 SELECT를 실행하는 트랜잭션에서 획득하며 트랜잭션 종료 시 해제된다.
 * @param lockId advisory lock 식별자로 사용할 64비트 정수.
 * @param statement 실행할 매퍼 구문 식별자(`mapperId.statementId`).
 * @param params SQL 템플릿에 바인딩할 파라미터 값.
 * @return 조회된 행 목록. 조회 결과가 없거나 DB 비활성화·잠금 또는 SQL 실행 예외 처리 시 빈 목록.
 */
DbTemplate::TypedRowSet selectWithAdvisoryLock(
    std::int64_t lockId, const std::string& statement, const DbTemplate::ParamMap& params);

} // namespace AppDb

#endif // __DB_RUNTIME_HPP__
