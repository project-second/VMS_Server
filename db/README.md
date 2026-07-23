# DB Migrations

이 디렉터리는 VMS 서버가 사용하는 PostgreSQL schema migration을 보관한다.

## 최초 설치

최초 설치에서는 `001`만 실행하지 않고 `db/migrations`의 모든 SQL을 번호순으로 적용한다.
`001_core_schema.sql`은 최신 최초 설치용 기본 스키마이고, 이후 번호의 파일은 기존 설치
업그레이드와 최초 설치 후속 변경을 함께 담당한다.

```bash
export VMS_DB_CONNINFO="postgresql://vms_user:veda1234@localhost:5432/vms_db"

for migration in db/migrations/*.sql; do
  psql "$VMS_DB_CONNINFO" -v ON_ERROR_STOP=1 -f "$migration" || exit 1
done
```

`ON_ERROR_STOP=1`은 SQL 오류가 발생했는데도 다음 마이그레이션을 계속 적용하는 것을
방지한다. 파일명 앞의 번호가 실행 순서이므로 마이그레이션 파일의 이름을 임의로 변경하면
안 된다.

## 기존 데이터베이스 업데이트

먼저 적용된 버전을 확인한다.

```bash
psql "$VMS_DB_CONNINFO" -c \
  "SELECT version, applied_at FROM schema_migrations ORDER BY version;"
```

그다음 아직 기록되지 않은 마이그레이션을 번호순으로 적용한다. 예를 들어
`003_archive_segment`가 없다면 다음과 같이 적용한다.

```bash
psql "$VMS_DB_CONNINFO" -v ON_ERROR_STOP=1 \
  -f db/migrations/003_archive_segment.sql
```

현재 마이그레이션은 재실행해도 이미 생성된 객체와 버전 기록을 건너뛰도록 작성되어
있지만, 운영 환경에서는 `schema_migrations`를 확인하고 미적용 파일만 실행하는 것을
권장한다.

## 실행 권한

마이그레이션은 테이블 생성과 `ALTER TABLE` 권한이 있는 DB 소유자 또는 전용 migration
계정으로 실행해야 한다. 애플리케이션 계정에 DDL 권한이 없다면 PostgreSQL 관리자
계정으로 실행한다.

로컬 서버에서 PostgreSQL OS 계정을 사용하는 예시는 다음과 같다.

```bash
sudo -u postgres psql -d vms_db -v ON_ERROR_STOP=1 \
  -f db/migrations/003_archive_segment.sql
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
- archive_segment
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
- `archive_segment(file_path)`
- `ptz_preset(channel_id, preset_token)`

Playback 시간 범위 조회를 위해 다음 인덱스도 포함한다.

- `archive_segment(profile_id, start_ts)`
- `archive_segment(status)`

## 주의 사항

현재 migration runner는 별도로 없다. 운영 배포에서는 위 순서대로 `psql`로 직접
적용하거나, 추후 서버 기동 전 migration runner를 추가해야 한다.
