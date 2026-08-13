# VMS Server 클래스 다이어그램

이 문서는 VMS Server의 C++17 코드를 기준으로 작성한 클래스 다이어그램 안내서다. `01`~`06`은 `src/`와 `include/`를 분석하며 `lib/` 타입을 경계 클래스로만 표시한다. `07`~`09`는 별도 요청에 따라 `lib/app-media-server` 내부 구현까지 확장해 분석한다.

## 다이어그램 구성

| 원본 | 바로 보기 | 내용 |
|---|---|---|
| [`01-overview.puml`](01-overview.puml) | [`SVG`](svg/01-overview.svg) | 애플리케이션 조립, HTTP API, ONVIF 프로비저닝, 미디어·재생의 전체 의존 구조 |
| [`02-app-bootstrap.puml`](02-app-bootstrap.puml) | [`SVG`](svg/02-app-bootstrap.svg) | 설정 로딩, 런타임 경로, 애플리케이션 시작 및 관리자 생성 명령 |
| [`03-api-auth-admin.puml`](03-api-auth-admin.puml) | [`SVG`](svg/03-api-auth-admin.svg) | 사용자·관리자 API, 인증 미들웨어, 서비스와 저장소 |
| [`04-api-device.puml`](04-api-device.puml) | [`SVG`](svg/04-api-device.svg) | 장비·채널·PTZ·프리셋·투어 API와 데이터 타입 |
| [`05-api-playback.puml`](05-api-playback.puml) | [`SVG`](svg/05-api-playback.svg) | 녹화 세그먼트 조회와 HLS VOD 재생 흐름 |
| [`06-onvif-media.puml`](06-onvif-media.puml) | [`SVG`](svg/06-onvif-media.svg) | ONVIF 탐색·프로비저닝, DB 동기화, 카메라·RTSP 등록 |
| [`07-media-server-overview.puml`](07-media-server-overview.puml) | [`SVG`](svg/07-media-server-overview.svg) | `app-media-server`의 녹화·릴레이와 Playback/HLS 전체 구조 |
| [`08-media-server-recording.puml`](08-media-server-recording.puml) | [`SVG`](svg/08-media-server-recording.svg) | 카메라 worker, GStreamer 녹화 pipeline, segment 상태, RTSP relay 상세 |
| [`09-media-server-playback-hls.puml`](09-media-server-playback-hls.puml) | [`SVG`](svg/09-media-server-playback-hls.svg) | 녹화 segment에서 PlaybackSession과 HLS VOD를 만드는 상세 흐름 |

생성된 SVG는 브라우저에서 바로 열 수 있다. 현재 결과물은 PlantUML 1.2026.6으로 문법 검사하고 렌더링했다. PlantUML 원본을 수정한 후에는 프로젝트 루트에서 다음과 같이 다시 렌더링한다.

```bash
plantuml -tsvg -o svg docs/class-diagrams/0*.puml
```

`plantuml` 실행 파일 대신 JAR를 사용하는 환경은 다음 명령을 사용한다.

```bash
java -jar plantuml.jar -tsvg -o svg docs/class-diagrams/0*.puml
```

출력 위치는 `docs/class-diagrams/svg/`이다. 각 원본은 같은 디렉터리의 `_style.puml`을 포함하므로 파일을 이동할 때 함께 옮겨야 한다.

## 표기 기준

- `*--`: 값 또는 `unique_ptr`로 소유하는 구성 관계
- `o--`: `optional` 또는 컬렉션으로 보유하는 관계
- `-->`: 참조 멤버로 연결된 연관 관계
- `..>`: 생성, 호출, 인자·반환 타입 등 일시적 의존 관계
- `<<lib boundary>>`: `lib/`에서 제공되며 내부 상세는 이 다이어그램에서 생략한 타입
- `<<source module>>`: C++ 클래스가 아니라 관련 자유 함수를 묶어 나타낸 소스 모듈

다이어그램은 선언부뿐 아니라 생성자 초기화와 구현부 호출도 함께 대조했다. 다만 표준 라이브러리 타입, JSON 타입, 로깅 유틸리티와 단순 문자열 헬퍼는 구조 이해에 직접 필요하지 않아 생략했다.

## 분석상 주의점

- `VmsApplication`은 대부분의 객체를 멤버로 보유하지 않고 `run()`의 지역 객체로 생성하여 수명을 조립한다. 따라서 개요도에서는 점선 `creates/wires` 의존 관계로 표시했다.
- `VmsEventController`는 클래스와 라우트 구현이 존재하지만 현재 `VmsApplication::run()`에서는 생성되지 않는다.
- `VmsDeviceService`에는 `OnvifPtzClient&` 멤버가 있으나 현재 PTZ 작업은 장비별 인증 설정을 사용해 지역 클라이언트를 새로 만든다.
- `OnvifDeviceProvisioningService::deviceClients` 멤버는 선언되어 있지만 현재 프로비저닝 루프는 작업별 지역 `OnvifDeviceClient`와 `OnvifMediaClient`를 사용한다.
- 사용자 access/refresh token 세션은 현재 `VmsUserService.cpp`의 프로세스 메모리 전역 맵으로 관리된다.

### `app-media-server` 분석상 주의점

- `CameraManager`는 `CameraWorker`들을 `unique_ptr`로 소유하고 모든 worker가 하나의 `RecordingSegmentStore`를 공유한다.
- `CameraWorker`는 연결 재시도마다 지역 `CameraPipeline`을 새로 생성한다. 오류나 EOS가 발생하면 파이프라인을 정리하고 3초 후 재연결한다.
- `CameraPipeline`은 하나의 입력을 MP4 fragment 녹화 branch와 RGB appsink branch로 나눈다. 현재 appsink branch는 프레임 정보를 확인하며 외부 소비자에게 프레임을 전달하지는 않는다.
- `RtspRelayServer`는 녹화 pipeline을 재사용하지 않고 별도 `rtspsrc`로 upstream 카메라에 연결한다.
- `RecordingSegmentStore`는 프로세스 메모리에 segment 상태를 보관하고, lock 밖에서 변경 callback을 실행한다. `VmsApplication`은 이 callback으로 DB 저장을 연결한다.
- `PlaybackService`는 `RecordingSegmentStore` 직접 조회와 호출자가 전달한 segment 목록 처리 두 방식을 지원한다. 현재 `VmsPlaybackService`는 DB 조회 결과를 후자의 방식으로 전달한다.
- `HlsVodConverter`는 H264 MP4를 decode/re-encode하지 않고 MPEG-TS로 remux하며 key-unit seek를 사용한다.

## 소스 영역 매핑

| 영역 | 주요 파일 |
|---|---|
| 시작·설정 | `src/main.cpp`, `src/app/`, `src/ApplicationConfigLoader.cpp`, `include/app/`, `include/ApplicationConfigLoader.h` |
| 사용자·관리자 API | `src/vms-api/VmsUser*`, `src/vms-api/VmsAdmin*`, 대응 `include/vms-api/` 헤더 |
| 장비·PTZ API | `src/vms-api/VmsDevice*`, 대응 `include/vms-api/` 헤더 |
| 재생 API | `src/vms-api/VmsPlayback*`, 대응 `include/vms-api/` 헤더 |
| ONVIF·미디어 조정 | `src/onvif-manager/`, `src/media/`, 대응 `include/` 헤더 |
| 미디어 서버 라이브러리 | `lib/app-media-server/include/`, `lib/app-media-server/*.cpp` |
