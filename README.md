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
  resources/application.yml
  resources/db/users.yml
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

기본 패키지 형식은 `TGZ`이며, 생성된 `.tar.gz` 파일에는 실행 파일과 `resources` 디렉터리가 함께 포함됩니다.

패키지를 풀고 실행하려면 다음과 같이 진행합니다.

```bash
tar -xzf vms_server-0.1.0-Linux.tar.gz
cd vms_server-0.1.0-Linux
./bin/vms_server
```


## 데이터베이스 설정
- USER: vms_user
- PASSWORD: veda1234
- DATABASE: vms_db

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
