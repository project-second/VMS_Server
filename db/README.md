# DB Migrations

이 디렉터리는 VMS 서버가 사용하는 PostgreSQL schema migration을 보관한다.

문서 기준일: 2026-10-20. 아래 내용은 migration SQL과 `resources/db/*.yml`을 대조한
결과이며, 실제 운영 DB의 적용 상태는 `schema_migrations`에서 확인해야 한다.

## 마이그레이션 목록

| 순서 | 파일 / 기록되는 version | 주요 변경 |
| --- | --- | --- |
| 1 | `001_core_schema.sql` / `001_core_schema` | 사용자·승인·역할, 장치·채널·프로필, 녹화 세그먼트, PTZ 테이블과 인덱스 생성. `ADMIN`, `OPERATOR`, `VIEWER` 역할 등록 |
| 2 | `002_ptz_tour_token.sql` / `002_ptz_tour_token` | 기존 `ptz_tours`에 ONVIF 투어 식별용 `tour_token` 컬럼과 채널별 토큰 unique index 추가 |
| 3 | `003_archive_segment.sql` / `003_archive_segment` | 기존 녹화 메타데이터를 `profile_id` 기반 구조로 보완하고 필수값·외래키·인덱스 설정 |

현재 `001`에는 `tour_token`과 `archive_segment`도 포함되어 있다. 이전 버전의
`001`을 적용한 DB에는 해당 구조가 없을 수 있으므로 `001`의 기록만으로 최신 상태라고
판단하지 않는다. `002`, `003` 적용 기록도 확인해야 한다.

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

`schema_migrations`가 없다면 버전 관리 이전에 수동 생성한 DB인지 확인한다. 기존
테이블이 있는 DB에 최초 설치 명령을 바로 실행하면 스키마 차이로 실패할 수 있다.
테이블과 데이터를 migration SQL과 대조한 뒤 적용 순서를 결정한다.

서버는 migration 적용 여부를 자동 검사하거나 SQL을 자동 실행하지 않는다. 기존
DB에서는 미적용 파일만 번호순으로 적용하고, 마지막에 위 조회 명령으로 기록을 다시
확인한다. 현재 전체 적용 시 기대하는 버전은 `001_core_schema`,
`002_ptz_tour_token`, `003_archive_segment`이다.

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

`001_core_schema.sql`에 정의된 애플리케이션 테이블은 다음과 같다. 적용 이력은 별도의
`schema_migrations` 테이블에 저장한다.

| 범위 | 테이블 | 관련 mapper |
| --- | --- | --- |
| 사용자·승인·역할 | `users`, `user_approvals`, `roles`, `user_roles` | `resources/db/users.yml` |
| 장치·상태·채널·프로필 | `devices`, `device_health`, `device_health_log`, `channels`, `profiles` | `resources/db/onvif.yml`, `resources/db/devices.yml` |
| PTZ | `ptz_capabilities`, `ptz_preset`, `ptz_tours`, `ptz_tours_items` | `resources/db/devices.yml` |
| 녹화·재생 | `archive_segment` | `resources/db/playback.yml` |

중복 방지와 mapper의 `ON CONFLICT` 처리를 위해 다음 unique index도 포함한다.

- `users(login_id)`
- `roles(role_code)`
- `devices(uuid)`
- `channels(device_id, channel_no)`
- `profiles(channel_id, onvif_profile_token)`
- `archive_segment(file_path)`
- `ptz_preset(channel_id, preset_token)`
- `ptz_tours(channel_id, tour_token)`

프로필·프리셋·투어의 토큰 인덱스는 토큰이 `NULL` 또는 빈 문자열인 행을 제외하는
partial unique index다. 프리셋·투어 upsert mapper의 `ON CONFLICT`에도 같은 조건이
포함된다.

Playback 시간 범위 조회를 위해 다음 인덱스도 포함한다.

- `archive_segment(profile_id, start_ts)`
- `archive_segment(status)`

`archive_segment`는 파일별 녹화 메타데이터를 저장하며 `profile_id`로 프로필과 연결한다.
녹화 시작 시 `recording`, 정상 종료 시 `completed`, 중단 시 `interrupted` 상태를
기록한다. Playback 목록·시간 범위 조회는 `completed` 또는 `interrupted`이고
`end_ts`가 있는 세그먼트를 대상으로 한다. 서버 시작 시 남아 있는 `recording` 행은
`interrupted`와 `has_gap = true`로 갱신한다.

## 기존 녹화 메타데이터 변환 (`003`)

`003_archive_segment.sql`은 수동 생성한 구형 `archive_segment`도 보완한다. 적용 전에는
DB를 백업하고 다음 변환 조건을 확인한다.

- 구형 `channel_id`가 있으면 해당 채널의 `channels.use_profile`과 일치하는
  `profiles.profile_no`를 우선 선택한다. 일치하는 프로필이 없으면 `profile_no`,
  `profile_id`가 작은 순서로 하나를 선택한다. 변환 후 `channel_id` 컬럼은 삭제한다.
- 프로필을 찾지 못하거나 `profile_id`, `file_path`, `start_ts`가 누락된 기존 행이
  있으면 오류로 중단한다. 비어 있는 `file_path`도 허용하지 않는다.
- 빈 `status`는 `end_ts` 유무에 따라 `recording` 또는 `completed`로 채우고,
  `has_gap`의 `NULL`은 `false`로 바꾼다.
- 중복 `file_path`나 존재하지 않는 프로필 참조는 unique index 또는 외래키 설정 시
  실패할 수 있다. SQL이 기존 데이터의 중복이나 잘못된 참조를 자동 삭제하지는 않는다.

`003`은 트랜잭션으로 실행되므로 실패 시 해당 파일의 변경은 롤백된다. 오류 원인과
기존 데이터를 확인한 뒤 다시 적용한다.

## 주의 사항

현재 migration runner는 별도로 없다. 운영 배포에서는 위 순서대로 `psql`로 직접
적용하거나, 추후 서버 기동 전 migration runner를 추가해야 한다.

- SQL은 스키마 이름을 명시하지 않는다. 적용 계정과 애플리케이션 계정이 같은 대상
  스키마를 사용하도록 `search_path`를 확인한다.
- `IF NOT EXISTS`와 버전 기록의 `ON CONFLICT DO NOTHING`은 객체·기록의 중복 생성을
  피한다. 기존 컬럼의 타입이나 제약을 전부 최신 구조로 교정하는 기능은 아니다.
- `001`을 재실행하면 기본 역할의 이름도 현재 SQL 값으로 갱신된다.
- `001`, `003`은 `BEGIN`/`COMMIT`을 포함하지만 `002`는 명시적 트랜잭션이 없다.
  `ON_ERROR_STOP=1`은 후속 실행을 멈추며, 이미 커밋된 이전 파일이나 `002`의 문장을
  되돌리지는 않는다. 실패했다면 버전 기록과 실제 객체 상태를 함께 확인한다.
- `schema_migrations`는 버전과 적용 시각만 기록한다. SQL 파일 변경 여부나 현재
  스키마의 일치 여부를 검증하지 않으므로 버전 행을 수동 추가해서 적용을 대신하지 않는다.
