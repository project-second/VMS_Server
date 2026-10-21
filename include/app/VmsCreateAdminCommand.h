#ifndef VMS_CREATE_ADMIN_COMMAND_H
#define VMS_CREATE_ADMIN_COMMAND_H

#include "ApplicationConfigLoader.h"

/**
 * @brief 실행 파일 이름을 포함해 서버 실행 및 최초 관리자 생성 명령의 사용법을 표준 오류로 출력한다.
 * @param program 사용법에 표시할 실행 파일 이름 또는 경로.
 */
void printVmsUsage(const char* program);

/**
 * @brief create-admin 명령의 인자와 비밀번호를 검증한 뒤 최초 관리자 계정을 생성한다.
 * @param argc 실행 파일 이름과 하위 명령을 포함한 명령행 인자 수.
 * @param argv 실행 파일 경로, create-admin, --id 및 --name 옵션과 값.
 * @param config 관리자 생성에 사용할 DB 연결 정보와 SQL 매퍼 경로를 담은 설정.
 * @retval 0 관리자 생성 성공.
 * @retval 1 관리자 생성 처리 실패.
 * @retval 2 명령행 인자 또는 비밀번호 입력 오류.
 * @retval 3 기존 관리자가 있어 생성 거부.
 * @retval 4 로그인 ID 중복.
 * @retval 5 ADMIN 역할 누락.
 */
int runCreateAdminCommand(int argc, char const* argv[], const AppRuntimeConfig& config);

#endif // VMS_CREATE_ADMIN_COMMAND_H
