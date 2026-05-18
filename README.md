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

## 데이터베이스 설정
- USER: vms_user
- PASSWORD: veda1234
- DATABASE: vms_db