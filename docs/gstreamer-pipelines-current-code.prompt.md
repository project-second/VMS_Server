# 현재 코드 기준 GStreamer 파이프라인 이미지 프롬프트

사용 도구: Codex 내장 이미지 생성 도구

## 코드 분석 범위

- `lib/app-media-server/CameraPipeline.cpp`
- `lib/app-media-server/H264RelayHub.cpp`
- `lib/app-media-server/RtspRelayServer.cpp`
- `lib/app-media-server/HlsVodConverter.cpp`
- `lib/app-media-server/CameraManager.cpp`
- `lib/app-media-server/CameraWorker.cpp`
- `src/media/MediaService.cpp`
- `src/app/VmsApplication.cpp`

현재 코드에서 확인되는 GStreamer 실행 구조는 다음 세 가지다.

1. ONVIF 프로필별 장기 실행 `CameraPipeline`
2. RTSP 클라이언트 접속 시 준비되는 shared `RtspRelayServer` media pipeline
3. 재생 요청 시 MP4 조각마다 생성되는 `HlsVodConverter` 임시 pipeline

## 최종 생성 프롬프트

프로젝트 기술 문서용 16:9 가로형 다이어그램. 순백색 배경, 얇고 선명한 직사각형과 직선 화살표, 한글 산세리프 글꼴을 사용한다. 위아래 두 패널로 구성하고 장식, 아이콘, 그림자, 그라데이션, 3D 효과, 워터마크를 사용하지 않는다.

제목은 `현재 코드 기준 GStreamer 파이프라인`이다.

### A. 실시간 수집 · 녹화 · RTSP Relay

`IP 카메라 / RTSP / H.264 → CameraPipeline (ONVIF 프로필별)`로 입력 화살표는 하나만 사용한다.

CameraPipeline 공통 경로:

`rtspsrc (latency=100) → rtph264depay → h264parse (config-interval=-1) → tee`

tee는 정확히 두 갈래로 분기한다.

- 녹화 분기: `queue → splitmuxsink (max-size-time) → MP4 조각`
- Relay feed: `queue (60 buffers · leaky) → appsink (max-buffers=1 · drop)`

Relay feed에는 `Relay 활성화 시`, `인코딩 H.264 샘플`을 표기한다.

파이프라인 경계의 연결은 정확히 다음 하나다.

`appsink → H264RelayHub → appsrc`

H264RelayHub에는 `caps 보관`, `키프레임부터 전달`, `타임스탬프 재생성`을 표기한다.

RtspRelayServer는 `shared media`, `클라이언트 접속 시 생성`을 표시하고 다음 순서로 그린다.

`appsrc (live · non-blocking) → queue (60 buffers · leaky) → h264parse → rtph264pay (PT=96 · config-interval=1) → /live/{camera_id} → RTSP 클라이언트 1, RTSP 클라이언트 N`

상단 요약은 `카메라 입력 연결: 프로필당 1개`, `디코딩 · 재인코딩 없음`, `RGB 프레임 추출 없음`이다.

### B. 재생 요청 시 MP4 → HLS VOD

입력 노드는 `녹화된 MP4 조각`이며 위에 `위 패널의 녹화 결과`를 표시한다. 입력 화살표는 filesrc로 한 번만 연결한다.

HlsVodConverter는 `조각별 임시 파이프라인`이며 다음 순서로 그린다.

`filesrc → qtdemux → queue → h264parse → capsfilter (byte-stream · AU) → mpegtsmux → filesink → .ts 조각`

생명주기는 `PAUSED → KEY_UNIT seek → PLAYING → EOS`로 표시한다.

GStreamer 외부 작업은 점선 경계로 분리한다.

`.ts 조각 → HlsVodPlaylistBuilder → index.m3u8`

하단 요약은 `구간 seek 후 remux`, `디코딩 · 재인코딩 없음`이다.

## 교정 사항

- 깨진 `encoded` 영문 표기를 `인코딩 H.264 샘플`로 교체
- 가장자리 생성 흔적과 불필요한 화살표 제거
- MP4에서 HLS converter 중앙으로 중복 연결되던 세로 점선 제거
- 아래 패널의 `녹화된 MP4 조각 → filesrc` 입력만 유지
