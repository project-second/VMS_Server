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

## 데이터베이스 설정
- USER: vms_user
- PASSWORD: veda1234
- DATABASE: vms_db