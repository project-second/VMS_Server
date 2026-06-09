# app-media-server

`app-media-server`는 VMS 서버의 live ingest, recording, playback, download 기능을 담당할 미디어 모듈이다.

현재 구현 범위는 첫 단계 PoC로 제한한다.

- RTSP live stream 수신 시작
- RTSP live stream 중지
- stream 상태 snapshot 조회
- GStreamer 기반 H.264/H.265 RTP depay/parse 확인

아직 recording segment 생성, live relay, playback, seek, download는 구현하지 않았다.

## 현재 구조

```text
lib/app-media-server/
  CMakeLists.txt
  MediaLiveStreamService.cpp
  include/
    MediaLiveStreamService.h
  README.md
```

## 사용 기술

현재 모듈은 GStreamer를 사용한다.

필요한 pkg-config package:

- `gstreamer-1.0`
- `gstreamer-app-1.0`
- `gstreamer-video-1.0`
- `gstreamer-rtp-1.0`
- `gstreamer-rtsp-1.0`
- `gstreamer-rtsp-server-1.0`

현재 컨테이너에는 RTSP 수신과 segment PoC에 필요한 주요 plugin이 이미 설치되어 있다.

- `rtspsrc`
- `rtph264depay`, `rtph265depay`
- `h264parse`, `h265parse`
- `splitmuxsink`
- `mp4mux`, `qtmux`
- `hlssink`, `hlssink2`
- `appsink`, `appsrc`

## Live 수신 사용법

가장 단순한 사용 예시는 다음과 같다.

```cpp
#include "MediaLiveStreamService.h"

MediaLiveStreamService media;

MediaLiveStreamOptions options;
options.streamId = "camera-1-main";
options.cameraId = "camera-1";
options.rtspUrl = "rtsp://192.168.0.10:554/stream1";
options.transport = MediaRtspTransport::Tcp;
options.latency = std::chrono::milliseconds(200);

if (!media.startStream(options)) {
    // start 실패 처리
}

std::optional<MediaLiveStreamSnapshot> snapshot = media.stream(options.streamId);
if (snapshot) {
    // snapshot->state
    // snapshot->lastError
}

media.stopStream(options.streamId);
```

여러 stream을 띄운 경우 전체 snapshot은 `streams()`로 조회한다.

```cpp
std::vector<MediaLiveStreamSnapshot> streams = media.streams();
```

프로세스 종료나 service 종료 시에는 `stopAll()`을 호출한다. `MediaLiveStreamService` destructor에서도 `stopAll()`을 호출한다.

```cpp
media.stopAll();
```

## 현재 Pipeline

현재 live 수신 pipeline은 연결 검증용이다. stream을 파일로 저장하거나 외부로 relay하지 않고 `fakesink`까지 흘려보낸다.

```text
rtspsrc
  -> H264 RTP depay -> h264parse -> fakesink
  -> H265 RTP depay -> h265parse -> fakesink
```

실제 구현에서는 이 위치에 recorder 또는 relay branch를 추가한다.

```text
rtspsrc
  -> depay
  -> parse
  -> tee
      -> recorder branch
      -> live relay branch
```

## 상태 모델

```text
Starting
Streaming
Stopping
Stopped
Failed
```

`Failed` 상태가 되면 `lastError`에 GStreamer error message가 들어간다.

## ONVIF와 연결

ONVIF Media Service가 구현되면 다음 순서로 연결한다.

```text
OnvifDiscoveryService
  -> OnvifDeviceClient.getCapabilities()
  -> mediaXAddr 확보
  -> OnvifMediaClient.GetProfiles
  -> OnvifMediaClient.GetStreamUri
  -> MediaLiveStreamService.startStream(rtspUrl)
```

초기 테스트에서는 ONVIF 없이 RTSP URL을 직접 넣어도 된다.

## 다음 작업

다음 단계는 recording segment 생성이다.

추천 순서:

1. `fakesink` 대신 `splitmuxsink`로 MP4 segment 저장
2. segment 파일명 규칙 정의
3. segment metadata DTO 추가
4. `media_segment` DB schema 추가
5. segment 목록 조회 API 연결
6. 개별 segment 재생/download API 연결
7. timeline 조회 구현
8. 범위 playback과 seek 구현

## 테스트용 gst-launch 예시

컨테이너에서 pipeline만 빠르게 확인하려면 다음 형태로 테스트할 수 있다.

```bash
gst-launch-1.0 rtspsrc location=rtsp://192.168.0.10:554/stream1 protocols=tcp latency=200 \
  ! application/x-rtp,media=video,encoding-name=H264 \
  ! rtph264depay ! h264parse ! fakesink sync=false
```

H.265 stream이면 다음처럼 바꾼다.

```bash
gst-launch-1.0 rtspsrc location=rtsp://192.168.0.10:554/stream1 protocols=tcp latency=200 \
  ! application/x-rtp,media=video,encoding-name=H265 \
  ! rtph265depay ! h265parse ! fakesink sync=false
```

## 현재 제한

- RTSP 인증 옵션 별도 모델 미구현
- H.264/H.265 video stream만 우선 고려
- audio branch 미구현
- recording 미구현
- playback 미구현
- live relay 미구현
- reconnect/backoff 미구현
- 실제 카메라 대상 장시간 테스트 미수행
