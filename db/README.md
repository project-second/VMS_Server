# DB Migrations

이 디렉터리는 VMS 서버가 사용하는 PostgreSQL schema migration을 보관한다.

## 적용 방법

개발 환경의 기본 설정을 그대로 쓴다면 다음처럼 적용한다.

```bash
psql "postgresql://vms_user:veda1234@localhost:5432/vms_db" \
  -f db/migrations/001_core_schema.sql
```

환경변수를 사용할 수도 있다.

```bash
export VMS_DB_CONNINFO="postgresql://vms_user:veda1234@localhost:5432/vms_db"
psql "$VMS_DB_CONNINFO" -f db/migrations/001_core_schema.sql
```

## CPack 배포

CPack 패키지에는 `resources/`와 함께 `db/` 디렉터리가 포함된다. 패키지 설치 후에는 애플리케이션 실행 전에 migration SQL을 대상 PostgreSQL에 먼저 적용해야 한다.

```text
패키지 설치
  -> db/migrations/*.sql 적용
  -> resources/application.yml DB 설정 확인
  -> vms_server 실행
```

## 현재 포함 범위

`001_core_schema.sql`은 현재 코드와 mapper가 직접 사용하는 테이블을 우선 포함한다.

- users
- user_approvals
- roles
- user_roles
- devices
- device_health
- device_health_log
- channels
- profiles
- ptz_capabilities
- ptz_preset
- ptz_tours
- ptz_tours_items

ONVIF provisioning bulk upsert를 위해 다음 unique constraint도 포함한다.

- `users(login_id)`
- `roles(role_code)`
- `devices(uuid)`
- `channels(device_id, channel_no)`
- `profiles(channel_id, onvif_profile_token)`
- `ptz_preset(channel_id, preset_token)`

## 주의 사항

현재 migration runner는 별도로 없다. 운영 배포에서는 `psql`로 직접 적용하거나, 추후 서버 기동 전 migration runner를 추가해야 한다.
