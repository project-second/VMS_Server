# VMS_Server

CCTV 장치를 관리하고 실시간 영상 중계, 녹화, 저장 영상 조회·재생을 제공하는 Linear CCTV 프로젝트의 VMS 서버입니다.

## 프로젝트 목표와 현재 상태

최근 코드 대조: **2026-10-20**. 아래 상태는 소스·설정·SQL을 확인한 결과이며, 실제 카메라나 배포 환경에서의 동작 검증 완료를 의미하지 않습니다.

목표는 ONVIF로 카메라를 탐색하고 RTSP 주소를 확보한 뒤, 영상 수신·중계·녹화와 저장 영상 조회·재생을 하나의 서버에서 연결하는 것입니다. 사용자 인증, 관리자 승인·권한 관리, PTZ 제어도 함께 제공합니다.

| 영역 | 현재 코드에 연결된 기능 | 남은 작업·검증 |
| --- | --- | --- |
| 사용자·관리자 | 회원가입·로그인·토큰 갱신, Bearer 인증, 관리자 승인·역할 관리, `create-admin` | 인증·승인 흐름의 통합 검증, 장치·채널별 접근 권한 정책 |
| 장치·ONVIF | 주기적 탐색, 장치·채널·프로필 DB 등록, RTSP 주소 확보 | provisioning의 장비별 인증정보 연결, 실제 장비·네트워크별 탐색 및 재연결 검증, 녹화 프로필 선택 정책 |
| PTZ | 이동·정지, 프리셋, 프리셋 투어 API와 ONVIF 호출 | 장비별 지원 기능·오류 응답과 DB 동기화 검증 |
| 실시간·녹화 | GStreamer H.264 수신, MP4 분할 녹화, 공유 H.264 입력을 사용하는 RTSP 중계, 녹화 메타데이터 DB 저장 | 다중 카메라·장시간 실행, 장애 복구, H.265·audio, 보관 기간·용량 관리 |
| 저장 영상 | DB 기반 목록·상세·서울 날짜 기준 타임라인, 최대 1시간 HLS VOD 생성, 세션 토큰을 사용하는 TS 응답 | 실제 플레이어 재생·seek, 변환 부하·캐시 제한, 다운로드·썸네일·개별 파일 재생 |
| 이벤트 | 샘플 HTTP/WebSocket 컨트롤러 코드 | 실제 이벤트 소스·인증 연결 및 애플리케이션 등록 |
| 배포 | CMake/Ninja 빌드, CPack TGZ 설치 규칙, 런타임 라이브러리·GStreamer plugin·SQL 포함 | 새 환경에서 패키지 실행 및 전체 migration 적용 검증 |

애플리케이션의 연결 지점은 [`VmsApplication.cpp`](src/app/VmsApplication.cpp)입니다. 저장 영상 API는 [`VmsPlaybackController.cpp`](src/vms-api/VmsPlaybackController.cpp), DB 저장·HLS 세션 처리는 [`VmsPlaybackService.cpp`](src/vms-api/VmsPlaybackService.cpp)에서 확인할 수 있습니다. HLS TS 요청은 세션별 토큰을 검사하며, 장치·채널별 사용자 권한 검사는 별도 후속 작업입니다.

### 다음 작업과 완료 기준 제안

아래는 현재 구현과 남은 항목을 기준으로 정리한 우선순위 제안입니다.

1. **핵심 영상 흐름 검증:** 실제 ONVIF 카메라의 탐색 → DB 등록 → 실시간 중계 → MP4 녹화 → 목록·타임라인 조회 → HLS 재생·seek를 연속 검증합니다. 연결 끊김 후 재연결과 서버 재시작 후 기존 녹화 조회도 확인합니다.
2. **배포 재현성 검증:** 새 DB에 migration 001~003을 적용하고, 패키지의 설정·라이브러리·plugin으로 관리자 생성 및 서버 실행이 가능한지 확인합니다.
3. **운영·기능 보완:** 녹화 대상 선택, 보관 기간·디스크 용량, HLS 변환·캐시 제한, 장치·채널별 권한을 정리한 뒤 다운로드·썸네일·실제 이벤트 기능을 구현합니다.

단순히 API나 클래스가 존재하는 상태와 위 시나리오가 통과한 상태를 구분해 진행 상황을 관리합니다.

### 관련 문서

- [구현 작업 흐름과 후속 작업](docs/06.%20구현%20작업%20흐름%20정리.md)
- [ONVIF gSOAP 개발 정리](docs/07.%20ONVIF%20gSOAP%20개발%20정리.md)
- [Media Server 기능·작업 목록](docs/08.%20Media%20Server%20개발%20작업%20목록.md)
- [DB migration 적용 안내](db/README.md)
- [구조 및 클래스 다이어그램](docs/class-diagrams/README.md)

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

현재 Compose는 미리 빌드된 `linear-cctv-vms-server-dev` 이미지와 기존 외부 volume `vms-postgres-data`를 사용합니다. 이미지 준비와 최초 DB 환경 구성은 위 `Docker-env` 저장소 안내를 따릅니다. 컨테이너는 PostgreSQL과 개발용 셸을 시작하며, `vms_server`는 빌드 후 별도로 실행합니다.

개발 컨테이너의 셸에 들어가려면 다음 명령을 사용합니다.

```bash
docker compose exec server bash
```

현재 Compose의 공개 포트는 `8080`, `5432`, `8554`입니다. `resources/application.yml`의 RTSP relay 포트는 `8555`이므로, 컨테이너 밖에서 중계 영상을 받으려면 Compose 포트 매핑과 relay 포트를 맞춰야 합니다. `app.media.rtspRelay.advertisedHost`도 클라이언트에서 접근할 수 있는 VMS 주소로 설정합니다.

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

현재 DB volume은 `external: true`로 선언되어 있어 `docker compose down`과 `docker compose down -v` 모두 이 volume을 삭제하지 않습니다. 외부 volume을 제거하지 않는 동작은 [Docker 공식 문서](https://docs.docker.com/reference/cli/docker/compose/down/)에서도 확인할 수 있습니다. DB 데이터를 유지하려면 `vms-postgres-data` volume 자체를 직접 삭제하지 않습니다.


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
  lib/gstreamer-1.0/*.so
  libexec/gstreamer-1.0/gst-plugin-scanner
  resources/application.yml
  resources/db/users.yml
  resources/db/onvif.yml
  resources/db/devices.yml
  resources/db/playback.yml
  db/README.md
  db/migrations/001_core_schema.sql
  db/migrations/002_ptz_tour_token.sql
  db/migrations/003_archive_segment.sql
```

애플리케이션은 실행 경로(`argv[0]`)의 디렉터리를 기준으로 `../resources/application.yml`을 읽습니다. 기본 개발 경로는 `build/vms_server`와 `resources/`, 배포 경로는 `bin/vms_server`와 `resources/`입니다. `app.db.mapperPath`가 상대 경로이면 실행 파일 디렉터리를 기준으로 해석합니다. 제공된 설정값 `../resources/db`는 두 구조에서 모두 같은 mapper 디렉터리를 가리킵니다.

녹화 경로처럼 별도로 정규화하지 않는 상대 경로는 현재 작업 디렉터리의 영향을 받으므로, 아래 예시는 설치 루트에서 실행합니다.

```bash
cd dist/vms_server
./bin/vms_server
```

### CPack 패키지 생성

배포용 압축 패키지는 프로젝트 루트에서 다음 명령으로 생성합니다.

```bash
cpack --config build/CPackConfig.cmake
```

기본 패키지 형식은 `TGZ`이며, 생성된 `.tar.gz` 파일에는 실행 파일, 런타임 shared library·GStreamer plugin용 `lib` 디렉터리, plugin scanner, `resources` 디렉터리, DB migration용 `db` 디렉터리가 함께 포함됩니다. Linux 설치 실행 파일은 `$ORIGIN/../lib`의 `DT_RPATH`를 사용하도록 구성되어 패키지 내부 라이브러리를 전이 의존성에서도 찾습니다. 런타임은 패키지 내부 plugin과 scanner 경로도 설정합니다. 실제 포함 파일은 빌드 환경에 설치된 의존성에 따라 확인해야 합니다.

패키지를 풀고 실행하려면 다음과 같이 진행합니다.

```bash
tar -xzf vms_server-0.1.0-Linux.tar.gz
cd vms_server-0.1.0-Linux
psql "postgresql://vms_user:veda1234@localhost:5432/vms_db" \
  -v ON_ERROR_STOP=1 \
  -f db/migrations/001_core_schema.sql \
  -f db/migrations/002_ptz_tour_token.sql \
  -f db/migrations/003_archive_segment.sql
```

모든 SQL 적용에 성공한 뒤 최초 관리자를 생성합니다. 기존 관리자가 있으면 이 단계는 생략합니다.

```bash
./bin/vms_server create-admin --id admin --name 관리자
```

관리자 계정이 준비되면 서버를 실행합니다.

```bash
./bin/vms_server
```

이미 같은 버전의 migration이 모두 적용된 DB라면 SQL 적용을 생략할 수 있습니다. migration은 서버가 자동 실행하지 않습니다. 재적용 범위와 기존 스키마의 제약은 [DB 안내](db/README.md)를 참고합니다.


## 데이터베이스 설정

저장소에 포함된 개발용 연결 정보입니다. 실제 서버 연결은 `resources/application.yml`의 `app.db.conninfo`에 설정합니다.

- USER: vms_user
- PASSWORD: veda1234
- DATABASE: vms_db

### DB Migration 배포

DB schema는 실행 파일에 내장하지 않고 SQL migration 파일로 배포합니다. CPack 패키지에는 다음 파일이 포함됩니다.

```text
db/README.md
db/migrations/001_core_schema.sql
db/migrations/002_ptz_tour_token.sql
db/migrations/003_archive_segment.sql
```

배포 대상 PostgreSQL에 schema를 적용하려면 패키지 루트에서 다음 명령을 실행합니다.

```bash
psql "postgresql://vms_user:veda1234@localhost:5432/vms_db" \
  -v ON_ERROR_STOP=1 \
  -f db/migrations/001_core_schema.sql \
  -f db/migrations/002_ptz_tour_token.sql \
  -f db/migrations/003_archive_segment.sql
```

`psql` 명령의 연결 문자열은 환경변수로 분리할 수도 있습니다. 이 변수는 아래 셸 명령용이며, 애플리케이션의 `application.yml` 값을 덮어쓰지 않습니다.

```bash
export VMS_DB_CONNINFO="postgresql://vms_user:veda1234@localhost:5432/vms_db"
psql "$VMS_DB_CONNINFO" -v ON_ERROR_STOP=1 \
  -f db/migrations/001_core_schema.sql \
  -f db/migrations/002_ptz_tour_token.sql \
  -f db/migrations/003_archive_segment.sql
```

현재 migration은 코드와 mapper가 사용하는 핵심 테이블을 생성합니다.

- `users`, `user_approvals`, `roles`, `user_roles`
- `devices`, `device_health`, `device_health_log`
- `channels`, `profiles`
- `ptz_capabilities`, `ptz_preset`, `ptz_tours`, `ptz_tours_items`
- `archive_segment`

`002`는 기존 `ptz_tours`에 ONVIF `tour_token`과 채널별 unique index를 추가합니다. `003`은 녹화 시작·종료 시각, 파일 경로·크기, codec, 상태를 저장하는 `archive_segment`를 생성합니다.

ONVIF provisioning과 저장 영상 조회를 위해 다음 unique/index도 함께 생성합니다.

- `users(login_id)`
- `roles(role_code)`
- `devices(uuid)`
- `channels(device_id, channel_no)`
- `profiles(channel_id, onvif_profile_token)`
- `ptz_tours(channel_id, tour_token)` — NULL이 아니고 빈 문자열이 아닌 token만 대상
- `archive_segment(file_path)`, `archive_segment(profile_id, start_ts)`, `archive_segment(status)`

배포 순서는 다음을 권장합니다.

```text
1. 패키지 압축 해제
2. db/migrations/001~003을 번호순 적용하고 성공 여부 확인
3. resources/application.yml의 DB 연결 정보 확인
4. ./bin/vms_server create-admin --id admin --name 관리자 실행
5. ./bin/vms_server 실행
```

## 애플리케이션 설정

애플리케이션 설정 파일은 `resources/application.yml`입니다.

설정은 `ApplicationConfigLoader`에서 한 번 읽고, 필요한 값은 `AppRuntimeConfig`로 바로 사용합니다. 설정 로더는 환경변수를 거치지 않고 `application.yml` 값을 `AppRuntimeConfig`에 직접 바인딩합니다.

### 설정 불러오기

설정 로더를 직접 `loadApplicationConfig(config)`로 호출하면 현재 작업 디렉터리의 `resources/application.yml`을 읽습니다. 실제 `VmsApplication`은 실행 파일 위치를 기준으로 경로를 구해 로더에 전달하고, 상대 mapper 경로도 정규화합니다.

```cpp
#include "ApplicationConfigLoader.h"
#include "DbRuntime.hpp"
#include "app/VmsRuntimePaths.h"

int main(int argc, char const* argv[]) {
    const auto paths = resolveRuntimePaths(argc > 0 ? argv[0] : nullptr);
    AppRuntimeConfig config;
    if (!loadApplicationConfig(config, paths.resourceDirectory.string(), "application.yml")) {
        return 1;
    }
    normalizeRuntimeConfigPaths(config, paths);

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
{"app.http.server.port", false, "8080", nullptr, &AppRuntimeConfig::serverPort, nullptr},
```

- 첫 번째 값: `application.yml`의 경로
- 두 번째 값: 필수 여부
- 세 번째 값: 설정이 없을 때 사용할 기본값
- 네 번째 값: `std::string` 타입의 `AppRuntimeConfig` 필드
- 다섯 번째 값: `int` 타입의 `AppRuntimeConfig` 필드
- 여섯 번째 값: `bool` 타입의 `AppRuntimeConfig` 필드

문자열·정수·불리언 필드 중 해당하는 한 곳에 멤버 포인터를 넣고 나머지는 `nullptr`로 둡니다. 정수 설정은 현재 로더에서 양수만 허용합니다.

새 설정을 코드에서 직접 쓰려면 먼저 `include/ApplicationConfigLoader.h`의 `AppRuntimeConfig`에 필드를 추가하고, `bindings()`에서 해당 필드에 연결합니다. 아래는 필드 추가 예시입니다.

```cpp
// include/ApplicationConfigLoader.h
struct AppRuntimeConfig {
    int requestTimeoutMs = 3000;
};

// src/ApplicationConfigLoader.cpp
{"app.http.requestTimeoutMs", false, "3000",
    nullptr, &AppRuntimeConfig::requestTimeoutMs, nullptr},
```

문자열 설정은 네 번째 필드에 연결합니다.

```cpp
// include/ApplicationConfigLoader.h
struct AppRuntimeConfig {
    std::string serviceName = "vms-server";
};

// src/ApplicationConfigLoader.cpp
{"app.service.name", false, "vms-server",
    &AppRuntimeConfig::serviceName, nullptr, nullptr},
```

불리언 설정은 여섯 번째 필드에 연결합니다. 현재 RTSP relay 설정의 예시는 다음과 같습니다.

```cpp
{"app.media.rtspRelay.enabled", false, "true",
    nullptr, nullptr, &AppRuntimeConfig::mediaRtspRelayEnabled},
```

필수 설정은 기본값을 `std::nullopt`로 두고 두 번째 값을 `true`로 둡니다. 현재 필수 설정은 DB 연결 문자열입니다. 설정 파일에 값이 없거나 비어 있으면 로딩에 실패합니다.

```cpp
{"app.db.conninfo", true, std::nullopt, &AppRuntimeConfig::dbConninfo, nullptr, nullptr},
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

비밀번호는 기존 로그인과 동일한 방식으로 해싱합니다. 이후 다음 DB 작업을 하나의 트랜잭션으로 처리합니다.

- 기존 관리자 존재 여부 확인 및 중복 생성 거부
- 활성화된 사용자와 `APPROVED` 승인 내역 생성
- `ADMIN` 역할 부여

동시에 여러 생성 명령이 실행되어도 최초 한 건만 생성됩니다. 관리자가 이미 존재하거나 같은 로그인 ID가 사용 중이면 새 계정을 만들지 않고 오류를 반환합니다.
