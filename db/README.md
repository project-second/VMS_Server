# DB Migrations

이 디렉터리는 VMS 서버가 사용하는 PostgreSQL schema migration을 보관한다.

문서 기준일: 2026-10-28. 아래 내용은 migration SQL과 `resources/db/*.yml`을 대조한
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

마이그레이션을 실행하기 전에 **PostgreSQL 서버, 접속 계정 `vms_user`, 데이터베이스
`vms_db`가 준비되어 있어야 한다.** 이 저장소의 SQL과 `vms_server`는 PostgreSQL 계정이나
데이터베이스를 자동 생성하지 않는다. SQL의 `users`, `roles`는 VMS 내부 회원·권한
테이블이며 PostgreSQL 접속 계정과는 별개다. `create-admin`도 VMS 관리자만 생성한다.

아래 쉘 명령은 소스 저장소 루트 또는 압축을 푼 패키지 루트처럼 `db/migrations/`가 있는
디렉터리에서 실행한다. PostgreSQL 콘솔에서 실행할 명령은 별도로 구분한다.

### 1. PostgreSQL 실행 상태와 포트 확인

Ubuntu/Debian에서 로컬 PostgreSQL 클러스터 상태를 확인한다.

```bash
pg_lsclusters
```

사용할 클러스터의 `Status`가 `online`인지, `Port`가 무엇인지 확인한다. PostgreSQL의
기본 포트는 `5432`지만 설치 상태에 따라 `5433` 등 다른 포트를 사용할 수 있다.
아래 예시는 `5432` 기준이므로 실제 포트가 다르면 관리자 접속 명령, `VMS_DB_CONNINFO`,
`resources/application.yml`의 포트를 모두 같은 값으로 맞춘다.

예를 들어 `16 main` 클러스터가 `down`이면 다음 명령으로 시작한 뒤 연결 응답을 확인한다.
버전과 클러스터 이름은 `pg_lsclusters` 결과에 맞게 바꾼다.

```bash
sudo pg_ctlcluster 16 main start
pg_isready -h localhost -p 5432
```

정상 응답은 `accepting connections`다. PostgreSQL 서버나 클러스터가 없다면 먼저
설치·구성해야 한다. Docker DB를 사용하는 경우에는 해당 컨테이너의 실행 상태와 포트
매핑을 확인하고, 계정·DB 생성도 그 PostgreSQL 서버에서 수행한다. Compose 실행만으로
`vms_user`와 `vms_db`가 준비된다고 가정하지 않는다.

### 2. PostgreSQL 접속 계정과 데이터베이스 생성

로컬 서버의 PostgreSQL 관리자 콘솔에 접속한다. `sudo`가 묻는 비밀번호는 Linux 사용자
비밀번호다. `-h`를 생략한 이 명령은 로컬 Unix socket으로 접속한다.

```bash
sudo -u postgres psql -p 5432 -d postgres
```

PostgreSQL 콘솔에서 기존 계정과 DB를 먼저 확인한다.

```text
\du vms_user
\l vms_db
```

둘 다 없다면 **같은 PostgreSQL 콘솔 안에서** 아래 명령을 실행한다.

```sql
CREATE ROLE vms_user LOGIN;
```

```text
\password vms_user
```

`\password`의 안내에 따라 DB 접속 비밀번호를 두 번 입력한다. 이어서 소유자가
`vms_user`인 DB를 생성한다.

```sql
CREATE DATABASE vms_db OWNER vms_user;
```

```text
\q
```

계정이나 DB가 이미 있으면 해당 `CREATE` 명령은 생략한다. 계정의 로그인 허용 여부와
DB 소유자를 확인하고, 비밀번호를 변경할 필요가 있을 때만 `\password vms_user`를
실행한다. 이 명령으로 비밀번호를 바꾸면 해당 계정을 사용하는 기존 서비스의 설정도
맞춰야 한다. 기존 DB의 소유권이나 데이터를 초기 설치를 위해 임의로 변경하지 않는다.

신규 DB에서는 DB 소유자인 `vms_user`로 마이그레이션을 적용하면 테이블과 시퀀스도
해당 계정 소유로 생성된다. 기존 DB의 객체 권한은 [실행 권한](#실행-권한)을 확인한다.

### 3. 애플리케이션 계정 접속 확인

일반 터미널로 돌아와 접속 정보를 지정하고, 방금 설정한 DB 비밀번호로 접속을 확인한다.
비밀번호는 명령 문자열에 넣지 않고 `-W`의 입력 요청에 따라 입력한다.

```bash
export VMS_DB_CONNINFO="host=localhost port=5432 dbname=vms_db user=vms_user"
psql "$VMS_DB_CONNINFO" -W -c '\conninfo'
```

이 단계에 실패하면 아래 [연결·권한 오류 확인](#연결권한-오류-확인)을 먼저 확인한다.
접속에 성공해야 마이그레이션 SQL을 실행할 수 있다.

### 4. 마이그레이션 적용과 결과 확인

최초 설치에서는 `001`만 실행하지 않고 `db/migrations`의 모든 SQL을 번호순으로 적용한다.
`001_core_schema.sql`은 최신 최초 설치용 기본 스키마이고, 이후 번호의 파일은 기존 설치
업그레이드와 최초 설치 후속 변경을 함께 담당한다.

```bash
psql "$VMS_DB_CONNINFO" -W -v ON_ERROR_STOP=1 \
  -f db/migrations/001_core_schema.sql \
  -f db/migrations/002_ptz_tour_token.sql \
  -f db/migrations/003_archive_segment.sql
```

`ON_ERROR_STOP=1`은 SQL 오류가 발생했는데도 다음 마이그레이션을 계속 적용하는 것을
방지한다. 파일명 앞의 번호가 실행 순서이므로 마이그레이션 파일의 이름을 임의로 변경하면
안 된다. 모든 파일은 같은 `psql` 접속에서 순서대로 실행되며, 비밀번호는 한 번 입력한다.

적용 후 아래 명령으로 `001_core_schema`, `002_ptz_tour_token`, `003_archive_segment`
세 버전의 기록이 있는지 확인한다.

```bash
psql "$VMS_DB_CONNINFO" -W -c \
  "SELECT version, applied_at FROM schema_migrations ORDER BY version;"
```

### 5. VMS 서버 연결 설정

`resources/application.yml`의 `app.db.conninfo`에도 위에서 확인한 호스트·포트·DB 이름·
사용자·비밀번호를 설정한다. 저장소의 개발용 예시 비밀번호와 실제 DB 비밀번호가
자동으로 일치하는 것은 아니다.
접속 문자열이 `postgresql://...` URI 형식이면 비밀번호의 예약 문자는 percent encoding이
필요하다. 예를 들어 비밀번호의 `@`는 URI에서 `%40`으로 작성한다.

`VMS_DB_CONNINFO`는 이 문서의 `psql` 명령에 사용하는 쉘 변수다. 이 변수를 `export`해도
`vms_server`의 연결 설정은 바뀌지 않으며, 서버는 `resources/application.yml`을 읽는다.

배포 패키지 루트에서는 최초 VMS 관리자를 생성한 뒤 서버를 실행한다. 이미 VMS 관리자가
있으면 `create-admin`은 생략한다. 소스에서 빌드한 경우 아래 실행 파일 경로를
`./build/vms_server`로 바꾼다.

```bash
./bin/vms_server create-admin --id admin --name 관리자
./bin/vms_server
```

## 기존 데이터베이스 업데이트

최초 설치 절차의 `VMS_DB_CONNINFO`를 사용할 DB에 맞게 설정한 뒤, 먼저 적용된 버전을
확인한다. 새 터미널에서는 쉘 변수를 다시 설정해야 한다.

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

신규 설치의 기본 절차는 `vms_user`를 DB 소유자로 지정하고 같은 계정으로 마이그레이션을
실행하는 것이다. 기존 DB는 대상 스키마에 대한 생성 권한과 기존 테이블의 소유권을
확인한다. 일반적인 데이터 조회·수정 권한만으로 `ALTER TABLE`을 실행할 수는 없다.

로컬 관리자 접속을 사용하더라도 **`vms_user`가 대상 객체를 변경할 권한이 있다면**
아래처럼 같은 세션에서 `SET ROLE`로 실행 역할을 지정할 수 있다. 이렇게 새로 만든
객체의 소유자는 `postgres`가 아닌 `vms_user`가 된다.

```bash
sudo -u postgres psql -p 5432 -d vms_db -v ON_ERROR_STOP=1 \
  -c 'SET ROLE vms_user;' \
  -f db/migrations/003_archive_segment.sql
```

`SET ROLE`은 `vms_user`에 없는 DDL 권한을 추가하지 않는다. 다른 계정이 소유한 기존
테이블을 변경할 때는 해당 객체의 소유자나 PostgreSQL 관리자 권한이 필요하다.
관리자 역할로 객체를 생성했다면 애플리케이션의 스키마·테이블·시퀀스 사용 권한도
별도로 설정해야 한다. DB에 대한 `GRANT`만으로 내부 객체의 권한이 모두 부여되지는 않는다.

## 연결·권한 오류 확인

| 오류 | 확인할 내용 |
| --- | --- |
| `Connection refused` / `no response` | 대상 PostgreSQL 서버의 실행 상태, 실제 포트, Docker 사용 시 포트 매핑을 확인한다. |
| `password authentication failed for user "vms_user"` | 관리자 콘솔에서 대상 클러스터의 계정 존재 여부와 로그인 허용 여부를 확인한다. 계정이 없어도 이 인증 오류가 나올 수 있다. 필요하면 비밀번호를 설정하고 접속 명령과 앱 설정을 맞춘다. |
| `database "vms_db" does not exist` | 접속한 클러스터에 DB가 있는지 확인한다. 신규 설치라면 계정을 먼저 만든 뒤 `CREATE DATABASE vms_db OWNER vms_user;`를 실행한다. |
| `permission denied for schema public` / `must be owner of table` | 적용 계정의 스키마 권한과 대상 테이블의 소유자를 확인한다. [실행 권한](#실행-권한)을 참고한다. |
| `relation ... does not exist` | 올바른 DB에 접속했는지, 마이그레이션 적용 기록과 실제 테이블, `search_path`가 일치하는지 확인한다. |

인증 오류는 SQL 파일을 읽어 실행하기 전 접속 단계에서 발생한다. PostgreSQL 역할·DB
생성 명령을 migration SQL에 넣는 것만으로는 이 접속 오류를 해결할 수 없다.

## CPack 배포

CPack 패키지에는 `resources/`와 함께 `db/` 디렉터리가 포함된다. 패키지 설치 후에는 애플리케이션 실행 전에 migration SQL을 대상 PostgreSQL에 먼저 적용해야 한다.

```text
패키지 설치
  -> 대상 PostgreSQL 서버·포트 확인
  -> PostgreSQL 접속 계정·DB 준비 (없는 경우 생성)
  -> db/migrations/*.sql 적용
  -> resources/application.yml DB 설정 확인
  -> 최초 VMS 관리자 생성 (필요한 경우)
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
