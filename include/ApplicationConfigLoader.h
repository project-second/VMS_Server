#ifndef APPLICATION_CONFIG_LOADER_H
#define APPLICATION_CONFIG_LOADER_H

#include <string>

/**
 * @brief application.yml에서 읽어 애플리케이션 구성에 사용하는 설정과 초기 기본값.
 */
struct AppRuntimeConfig {
    // DB 설정(app.db.*). conninfo는 반드시 지정해야 하는 항목이다.
    std::string dbConninfo; ///< DB 접속 문자열.
    std::string dbMapperPath = "resources/db"; ///< SQL 매퍼 디렉터리.

    // HTTP 서버 및 클라이언트 풀 설정(app.http.*).
    std::string serverHost = "0.0.0.0"; ///< HTTP 서버 수신 주소.
    int serverPort = 8080; ///< HTTP 서버 수신 포트.
    int serverThreads = 8; ///< HTTP 요청 처리 스레드 수.
    int clientPoolThreads = 8; ///< HTTP 클라이언트 풀 스레드 수 설정.
    int clientPoolMaxQueue = 500; ///< 클라이언트 풀 대기 작업 수 상한 설정.

    // ONVIF 장치 탐색 및 SOAP 통신 로그 설정(app.onvif.*).
    int onvifDiscoveryIntervalMs = 300000; ///< 장치 탐색 주기(ms).
    bool onvifSoapLogEnabled = false; ///< SOAP 로그 기록 여부.
    std::string onvifSoapLogDir = "/tmp"; ///< SOAP 로그 출력 디렉터리.

    // 녹화 및 RTSP 릴레이 설정(app.media.*).
    std::string mediaRecordingRootDir = "recordings/onvif"; ///< 녹화 파일 저장 루트.
    int mediaRecordingSplitSeconds = 5; ///< 녹화 파일 분할 간격(초).
    bool mediaRtspRelayEnabled = true; ///< RTSP 릴레이 사용 여부.
    std::string mediaRtspRelayBindAddress = "0.0.0.0"; ///< 릴레이 수신 주소.
    int mediaRtspRelayPort = 8555; ///< 릴레이 수신 포트.
    std::string mediaRtspRelayAdvertisedHost = "127.0.0.1"; ///< 클라이언트에 안내할 릴레이 호스트.

    int mediaMtxPlaybackSyncIntervalMs = 30000; ///< MediaMTX 재생 동기화 주기(ms) 설정(app.mediamtx.playbackSyncIntervalMs).
};

/**
 * @brief YAML 파일을 읽고 각 설정의 필수 여부와 값 형식을 검증해 config에 반영한다.
 * @param config 값을 반영할 설정. 실패하더라도 이미 처리한 일부 항목은 변경될 수 있다.
 * @param directory 설정 파일 디렉터리. 상대 경로는 현재 작업 디렉터리를 기준으로 한다.
 * @param file 읽을 YAML 파일 이름.
 * @return 파일 로딩과 모든 설정 검증에 성공하면 true, 오류가 있으면 false.
 */
bool loadApplicationConfig(
    AppRuntimeConfig& config,
    const std::string& directory = "resources",
    const std::string& file = "application.yml");

#endif // APPLICATION_CONFIG_LOADER_H
