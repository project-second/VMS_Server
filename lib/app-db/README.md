# app-db

`app-db`는 VMS 서버의 PostgreSQL 접근을 담당하는 공통 DB 모듈이다. SQL을 C++ 문자열로 직접 흩뿌리지 않고, `resources/db/*.yml` mapper 파일에 모아두고 `DbTemplate` 또는 `AppDb` runtime wrapper로 실행한다.

## 구성

```text
lib/app-db/
  DbTemplate.cpp
  DbRuntime.cpp
  include/
    DbTemplate.hpp
    DbRuntime.hpp
    DbRowHelpers.h
resources/db/
  users.yml
```

주요 타입은 다음과 같다.

- `DbTemplate`: mapper 기반 SQL 실행 객체. PostgreSQL connection을 하나 가진다.
- `AppDb`: 전역 runtime wrapper. `configure()` 이후 singleton `DbTemplate`을 lazy initialize한다.
- `DbRow`: `TypedRow`에서 string/int/bool 값을 꺼내는 helper namespace.

빌드 의존성은 `libpqxx`, PostgreSQL client library, `yaml-cpp`, `app-util`이다.

## 설정

애플리케이션 설정 예시는 `resources/application.yml`에 있다.

```yaml
app:
  db:
    conninfo: "postgresql://vms_user:password@localhost:5432/vms_db"
    mapperPath: "resources/db"
```

실행 초기에 설정값을 읽어 `AppDb::configure()`를 호출해야 한다.

```cpp
#include "DbRuntime.hpp"

AppDb::configure(
    "postgresql://vms_user:password@localhost:5432/vms_db",
    "resources/db");
```

`mapperPath`가 상대 경로이면 현재 working directory 기준 절대 경로로 해석된다. 설정하지 않았거나 연결 실패가 발생하면 `AppDb` wrapper 함수들은 실패 로그를 남기고 `false` 또는 빈 row set을 반환한다.

## Mapper 작성법

mapper 파일은 YAML 형식이며, namespace와 statement 목록을 가진다.

```yaml
mapper:
  namespace: users
  objects:
    - id: getUserByLoginId
      sequence: select
      query: >
        SELECT
          user_id,
          login_id,
          name
        FROM users
        WHERE login_id = #{login_id}
        LIMIT 1
```

statement 이름은 다음 형식으로 호출한다.

```text
namespace.id
```

예를 들어 위 mapper는 `users.getUserByLoginId`로 호출한다.

`sequence`는 실행 종류를 나타낸다.

- `insert`: `DbTemplate::insert()` 또는 `AppDb::insert()`에서 사용
- `update`: `DbTemplate::update()` 또는 `AppDb::update()`에서 사용
- `select`: `DbTemplate::selectTyped()` 또는 `AppDb::select()`에서 사용
- `delete`: `DbTemplate::deleteQ()` 또는 `AppDb::remove()`에서 사용

SQL 안의 `#{name}` placeholder는 내부에서 PostgreSQL positional parameter로 변환된다. 예를 들어 `#{login_id}`는 `$1`로 변환되고, 값은 `ParamMap`에서 이름으로 찾아 바인딩한다.

## ParamMap

SQL 파라미터는 `DbTemplate::ParamMap`으로 넘긴다.

```cpp
DbTemplate::ParamMap params{
    {"login_id", std::string("admin")},
    {"is_active", true},
    {"limit", std::int64_t(20)},
};
```

지원 값 타입은 다음과 같다.

```cpp
std::variant<std::monostate, bool, std::int64_t, double, std::string>
```

- `std::monostate{}`: SQL NULL
- `bool`: PostgreSQL boolean
- `std::int64_t`: 정수
- `double`: 실수/numeric
- `std::string`: 문자열

placeholder에 해당하는 key가 `ParamMap`에 없으면 예외가 발생한다. `AppDb` wrapper를 쓰면 예외는 내부에서 잡혀 로그로 남고 `false` 또는 빈 결과가 반환된다.

## AppDb Wrapper 사용법

대부분의 application/service/repository code에서는 `AppDb` wrapper를 쓰면 된다.

### Insert

```cpp
#include "DbRuntime.hpp"

bool ok = AppDb::insert("users.createUser", {
    {"login_id", std::string("admin")},
    {"name", std::string("Administrator")},
    {"pw_hash", std::string("hashed-password")},
});
```

### Update

```cpp
bool ok = AppDb::update("users.updateUserActiveStatus", {
    {"user_id", std::int64_t(1)},
    {"is_active", true},
});
```

### Delete

```cpp
bool ok = AppDb::remove("users.deleteUserRole", {
    {"user_id", std::int64_t(1)},
    {"role_id", std::int64_t(2)},
});
```

### Select

```cpp
#include "DbRuntime.hpp"
#include "DbRowHelpers.h"

DbTemplate::TypedRowSet rows = AppDb::select("users.getUserByLoginId", {
    {"login_id", std::string("admin")},
});

if (!rows.empty()) {
    std::string loginId;
    std::int64_t userId = 0;
    bool active = false;

    DbRow::getInt64Cell(rows.front(), "user_id", userId);
    DbRow::getStringCell(rows.front(), "login_id", loginId);
    DbRow::getBoolCell(rows.front(), "is_active", active);
}
```

조회 결과는 `TypedRowSet`이다.

```cpp
using TypedRow = std::unordered_map<std::string, CellValue>;
using TypedRowSet = std::vector<TypedRow>;
```

`CellValue`는 `std::variant<std::monostate, bool, std::int64_t, double, std::string>`이다. PostgreSQL OID 기준으로 bool/int/float/numeric은 가능한 한 타입을 보존하고, 그 외 값은 string으로 저장한다.

## DbTemplate 직접 사용

테스트나 별도 DB instance가 필요한 경우에는 `DbTemplate`을 직접 만들 수 있다.

```cpp
DbTemplate db(
    "postgresql://vms_user:password@localhost:5432/vms_db",
    "resources/db");

DbTemplate::TypedRowSet rows = db.selectTyped("users.getUserById", {
    {"user_id", std::int64_t(1)},
});
```

단, `DbTemplate`은 내부에 하나의 `pqxx::connection`을 가진다. 여러 thread에서 같은 `DbTemplate`을 직접 공유해 동시에 호출하는 구조는 피하는 것이 좋다. 공용 runtime을 쓸 때는 `AppDb` wrapper가 내부 mutex로 실행을 직렬화한다.

## Repository에서 쓰는 패턴

업무 로직에서는 DB 호출을 service에 직접 흩뿌리기보다 repository/DAO 객체로 모으는 편이 좋다.

```cpp
class UserRepository {
public:
    std::optional<UserRecord> findByLoginId(const std::string& loginId) {
        auto rows = AppDb::select("users.getUserByLoginId", {
            {"login_id", loginId},
        });

        if (rows.empty()) {
            return std::nullopt;
        }

        UserRecord user;
        DbRow::getInt64Cell(rows.front(), "user_id", user.userId);
        DbRow::getStringCell(rows.front(), "login_id", user.loginId);
        DbRow::getStringCell(rows.front(), "name", user.name);
        DbRow::getBoolCell(rows.front(), "is_active", user.active);
        return user;
    }
};
```

ONVIF provisioning 결과 저장도 같은 방식으로 별도 repository에 두는 것이 좋다.

```cpp
class OnvifProvisioningRepository {
public:
    bool upsertDevices(const std::vector<ProvisioningResult>& results) {
        for (const ProvisioningResult& result : results) {
            const bool ok = AppDb::insert("onvif.upsertDevice", {
                {"endpoint_reference", result.discoveredDevice.endpointReference},
                {"device_service_url", result.deviceServiceUrl},
                {"media_service_url", result.mediaServiceUrl},
            });

            if (!ok) {
                return false;
            }
        }
        return true;
    }
};
```

현재 `AppDb` wrapper는 호출마다 개별 transaction을 연다. 여러 statement를 하나의 transaction으로 묶는 batch API는 아직 없다. 강한 원자성이 필요한 저장 흐름은 추후 `DbTemplate`에 transaction callback이나 batch executor를 추가하는 편이 좋다.

## 동시성 주의점

- `AppDb` wrapper는 내부 mutex로 DB 실행을 직렬화한다.
- ThreadPool worker 여러 개에서 동시에 `AppDb::insert/select/update/remove`를 호출해도 connection 공유 충돌은 피한다.
- 대신 DB 작업은 병렬 처리되지 않고 순서대로 실행된다.
- ONVIF처럼 외부 I/O는 worker에서 병렬 수행하고, DB upsert는 결과를 모아 repository에서 처리하는 구조가 더 안정적이다.

## 제한 사항

- connection pool은 아직 없다.
- 명시적 multi-statement transaction/batch API는 아직 없다.
- mapper file은 statement 실행 시 다시 읽는다.
- `AppDb::instance()`는 처음 호출 시 lazy initialize된다. `configure()`는 첫 DB 호출보다 먼저 해야 한다.
