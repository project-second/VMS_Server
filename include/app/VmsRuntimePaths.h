#ifndef VMS_RUNTIME_PATHS_H
#define VMS_RUNTIME_PATHS_H

#include "ApplicationConfigLoader.h"

#include <filesystem>

/**
 * @brief 실행 파일과 함께 배포되는 리소스를 찾는 데 사용하는 기준 디렉터리.
 */
struct VmsRuntimePaths {
    /** @brief argv[0]을 절대 경로로 변환했을 때의 실행 파일 상위 디렉터리. */
    std::filesystem::path executableDirectory;
    /** @brief 실행 파일 디렉터리의 ../resources 경로를 정리한 리소스 디렉터리. */
    std::filesystem::path resourceDirectory;
};

/**
 * @brief argv[0]으로 전달된 경로를 기준으로 실행 파일 및 리소스 디렉터리를 계산한다.
 * @param argv0 프로그램 실행 시 전달된 실행 파일 경로.
 * @return 실행 파일 디렉터리와 배포 구조에 따른 리소스 디렉터리.
 */
VmsRuntimePaths resolveRuntimePaths(const char* argv0);

/**
 * @brief 설정의 DB 매퍼 경로가 상대 경로이면 실행 파일 디렉터리 기준의 절대 경로로 보정한다.
 * @param config DB 매퍼 경로를 보정할 런타임 설정.
 * @param paths 상대 경로의 기준이 되는 실행 파일 디렉터리 정보.
 */
void normalizeRuntimeConfigPaths(AppRuntimeConfig& config, const VmsRuntimePaths& paths);

/**
 * @brief GStreamer 초기화 전에 호출해 패키지에 포함된 라이브러리, 플러그인, 스캐너 경로를 설정한다.
 * @param paths 패키지 설치 위치를 계산하는 데 사용할 실행 파일 디렉터리 정보.
 */
void configurePackagedGStreamerEnvironment(const VmsRuntimePaths& paths);

#endif // VMS_RUNTIME_PATHS_H
