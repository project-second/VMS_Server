# VMS_Server

CCTV를 관리하는 VMS Server를 개발합니다.

## Development Environment

이 작업환경은 Linear CCTV 프로젝트의 `vms-server` 개발 환경입니다.

프로젝트를 실행하거나 개발 환경을 구성하려면 아래 Docker 개발 환경 저장소의 README를 먼저 참고해야 합니다.

- [project-second/Docker-env](https://github.com/project-second/Docker-env.git)

해당 저장소의 `vms-server` 환경은 Ubuntu 24.04 기반 Docker 이미지로 구성되어 있으며, VMS 서버 개발에 필요한 주요 패키지와 포트 설정을 포함합니다.

주요 포함 항목은 다음과 같습니다.

- gSOAP
- Nginx
- PostgreSQL
- GStreamer
- GStreamer RTSP Server
- OpenSSL
- C/C++ build tools

Docker 이미지 빌드, 컨테이너 실행, 로컬 소스코드 마운트 방법은 `Docker-env` 저장소 README의 `vms-server` 섹션을 기준으로 진행합니다.

## Docker Compose 실행

이 저장소에는 기존 `docker run` 실행 옵션을 옮긴 `docker-compose.yml`이 포함되어 있습니다.

컨테이너를 실행하려면 프로젝트 루트에서 다음 명령을 실행합니다.

```bash
docker compose up -d
```

실행 상태는 다음 명령으로 확인할 수 있습니다.

```bash
docker compose ps
```

로그는 다음 명령으로 확인합니다.

```bash
docker compose logs -f
```

기존에 같은 이름의 컨테이너가 `docker run`으로 실행되어 있다면 먼저 컨테이너만 중지하고 삭제한 뒤 실행합니다.

```bash
docker stop linear-cctv-vms-server-dev
docker rm linear-cctv-vms-server-dev
docker compose up -d
```

PostgreSQL 데이터는 기존 Docker volume인 `vms-postgres-data`를 사용합니다. 이 volume은 데이터베이스 파일을 보관하므로 삭제하면 안 됩니다.

컨테이너를 종료하려면 다음 명령을 사용합니다.

```bash
docker compose down
```

주의: 데이터베이스 volume까지 삭제될 수 있으므로 아래 명령은 사용하지 마세요.

```bash
docker compose down -v
```


## 프로젝트 빌드 및 배포

아래 명령은 Docker 개발 컨테이너 안에서 프로젝트 루트를 기준으로 실행합니다.

### 빌드

```bash
cmake -S . -B build -G Ninja
cmake --build build
```

빌드가 완료되면 실행 파일은 `build/vms_server`에 생성됩니다.

### 설치 구조 확인

CPack 패키지를 만들기 전에 `cmake --install`로 배포 구조를 먼저 확인할 수 있습니다.

```bash
cmake --install build --prefix dist/vms_server
```

설치 결과는 다음 구조를 가집니다.

```text
dist/vms_server/
  bin/vms_server
  lib/*.so*
  resources/application.yml
  resources/db/users.yml
  resources/db/onvif.yml
  db/README.md
  db/migrations/001_core_schema.sql
```

애플리케이션은 현재 작업 디렉터리 기준으로 `resources/application.yml`을 읽으므로, 설치 루트로 이동한 뒤 실행합니다.

```bash
cd dist/vms_server
./bin/vms_server
```

### CPack 패키지 생성

배포용 압축 패키지는 다음 명령으로 생성합니다.

```bash
cpack --config build/CPackConfig.cmake
```

기본 패키지 형식은 `TGZ`이며, 생성된 `.tar.gz` 파일에는 실행 파일, 런타임 shared library용 `lib` 디렉터리, `resources` 디렉터리, DB migration용 `db` 디렉터리가 함께 포함됩니다. 설치된 실행 파일은 `$ORIGIN/../lib` RUNPATH를 사용하므로, 패키지 내부 `lib/*.so*`를 우선 찾습니다.

패키지를 풀고 실행하려면 다음과 같이 진행합니다.

```bash
tar -xzf vms_server-0.1.0-Linux.tar.gz
cd vms_server-0.1.0-Linux
psql "postgresql://vms_user:veda1234@localhost:5432/vms_db" \
  -f db/migrations/001_core_schema.sql
./bin/vms_server
```

이미 migration이 적용된 DB라면 SQL 적용 단계는 생략할 수 있습니다. `001_core_schema.sql`은 `CREATE TABLE IF NOT EXISTS`, `CREATE INDEX IF NOT EXISTS` 중심으로 작성되어 반복 적용에 비교적 안전하게 구성되어 있습니다.


## 데이터베이스 설정
- USER: vms_user
- PASSWORD: veda1234
- DATABASE: vms_db

### DB Migration 배포

DB schema는 실행 파일에 내장하지 않고 SQL migration 파일로 배포합니다. CPack 패키지에는 다음 파일이 포함됩니다.

```text
db/README.md
db/migrations/001_core_schema.sql
```

배포 대상 PostgreSQL에 schema를 적용하려면 패키지 루트에서 다음 명령을 실행합니다.

```bash
psql "postgresql://vms_user:veda1234@localhost:5432/vms_db" \
  -f db/migrations/001_core_schema.sql
```

환경변수를 사용할 수도 있습니다.

```bash
export VMS_DB_CONNINFO="postgresql://vms_user:veda1234@localhost:5432/vms_db"
psql "$VMS_DB_CONNINFO" -f db/migrations/001_core_schema.sql
```

현재 migration은 코드와 mapper가 사용하는 핵심 테이블을 생성합니다.

- `users`, `user_approvals`, `roles`, `user_roles`
- `devices`, `device_health`, `device_health_log`
- `channels`, `profiles`

ONVIF provisioning과 향후 bulk upsert를 위해 다음 unique/index도 함께 생성합니다.

- `users(login_id)`
- `roles(role_code)`
- `devices(uuid)`
- `channels(device_id, channel_no)`
- `profiles(channel_id, onvif_profile_token)`

배포 순서는 다음을 권장합니다.

```text
1. 패키지 압축 해제
2. db/migrations/*.sql 적용
3. resources/application.yml의 DB 연결 정보 확인
4. ./bin/vms_server create-admin --id admin --name 관리자 실행
5. ./bin/vms_server 실행
```

## 애플리케이션 설정

애플리케이션 설정 파일은 `resources/application.yml`입니다.

설정은 `ApplicationConfigLoader`에서 한 번 읽고, 필요한 값은 `AppRuntimeConfig`로 바로 사용합니다. 설정 로더는 환경변수를 거치지 않고 `application.yml` 값을 `AppRuntimeConfig`에 직접 바인딩합니다.

### 설정 불러오기

런타임에서 사용할 설정은 `loadApplicationConfig(config)`로 불러옵니다. 기본값은 `resources/application.yml`이며, 다른 경로를 쓰려면 `loadApplicationConfig(config, directory, file)` 형태로 넘깁니다.

```cpp
#include "ApplicationConfigLoader.h"
#include "DbRuntime.hpp"

int main(int argc, char const* argv[]) {
    AppRuntimeConfig config;
    if (!loadApplicationConfig(config)) {
        return 1;
    }

    const std::string& db_conninfo = config.dbConninfo;
    const std::string& db_mapper_path = config.dbMapperPath;
    const std::string& host = config.serverHost;
    const int port = config.serverPort;
    const int server_threads = config.serverThreads;
    const int client_pool_threads = config.clientPoolThreads;
    const int client_pool_max_queue = config.clientPoolMaxQueue;

    AppDb::configure(config.dbConninfo, config.dbMapperPath);

    return 0;
}
```

### 설정 추가 가이드

`application.yml`에 설정을 추가할 때는 `src/ApplicationConfigLoader.cpp`의 `bindings()` 테이블에 매핑을 추가합니다.

매핑 한 줄에는 다음 정보가 함께 들어갑니다.

```cpp
{"app.http.server.port", false, "8080", nullptr, &AppRuntimeConfig::serverPort},
```

- 첫 번째 값: `application.yml`의 경로
- 두 번째 값: 필수 여부
- 세 번째 값: 설정이 없을 때 사용할 기본값
- 네 번째 값: `std::string` 타입의 `AppRuntimeConfig` 필드
- 다섯 번째 값: `int` 타입의 `AppRuntimeConfig` 필드

새 설정을 코드에서 직접 쓰려면 먼저 `include/ApplicationConfigLoader.h`의 `AppRuntimeConfig`에 필드를 추가하고, `bindings()`에서 해당 필드에 연결합니다.

```cpp
// include/ApplicationConfigLoader.h
struct AppRuntimeConfig {
    int requestTimeoutMs = 3000;
};

// src/ApplicationConfigLoader.cpp
{"app.http.requestTimeoutMs", false, "3000",
    nullptr, &AppRuntimeConfig::requestTimeoutMs},
```

문자열 설정은 네 번째 필드에 연결합니다.

```cpp
// include/ApplicationConfigLoader.h
struct AppRuntimeConfig {
    std::string serviceName = "vms-server";
};

// src/ApplicationConfigLoader.cpp
{"app.service.name", false, "vms-server",
    &AppRuntimeConfig::serviceName, nullptr},
```

필수 설정은 기본값을 `std::nullopt`로 두고 두 번째 값을 `true`로 둡니다. 설정 파일에 값이 없으면 로딩에 실패합니다.

```cpp
{"app.auth.secret", true, std::nullopt, &AppRuntimeConfig::authSecret, nullptr},
```

선택 설정은 두 번째 값을 `false`로 둡니다. 기본값이 있으면 세 번째 값에 넣고, 기본값 없이 생략 가능하면 `std::nullopt`를 사용합니다.

## 초기 관리자 생성

일반 회원가입과 별도로 `create-admin` 명령을 사용해 최초 관리자 계정을 한 번만 생성할 수 있습니다. 먼저 DB migration을 적용하고 `resources/application.yml`의 DB 연결 정보를 확인합니다.

개발 빌드에서는 프로젝트 루트에서 다음과 같이 실행합니다.

```bash
./build/vms_server create-admin \
  --id admin \
  --name 관리자
```

배포 패키지에서는 패키지 루트에서 다음과 같이 실행합니다.

```bash
./bin/vms_server create-admin \
  --id admin \
  --name 관리자
```

명령을 실행하면 비밀번호와 확인 비밀번호를 차례로 입력받습니다. 비밀번호는 화면에 표시되지 않으며 명령행 인자로 전달하지 않습니다.

생성 과정은 다음 작업을 하나의 DB 트랜잭션으로 처리합니다.

- 기존 관리자 존재 여부 확인 및 중복 생성 거부
- 기존 로그인과 동일한 방식으로 비밀번호 해싱
- 활성화된 사용자와 `APPROVED` 승인 내역 생성
- `ADMIN` 역할 부여

동시에 여러 생성 명령이 실행되어도 최초 한 건만 생성됩니다. 관리자가 이미 존재하거나 같은 로그인 ID가 사용 중이면 새 계정을 만들지 않고 오류를 반환합니다.