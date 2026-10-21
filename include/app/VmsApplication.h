#ifndef VMS_APPLICATION_H
#define VMS_APPLICATION_H

/**
 * @brief 설정 로딩, 구성 요소 연결, 서버 시작 및 종료를 조율하는 애플리케이션 진입점.
 */
class VmsApplication {
public:
    /**
     * @brief 인자 없이 실행하면 VMS 서버를 구동하고 종료 신호가 올 때까지 대기한다.
     *        create-admin 하위 명령은 서버를 구동하지 않고 관리자 생성만 수행한다.
     * @param argc 실행 파일 이름을 포함한 명령행 인자 수.
     * @param argv 실행 파일 경로(argv[0])와 하위 명령 및 옵션.
     * @return 정상 종료 시 0, 설정/서버 시작 실패 또는 하위 명령 오류 시 0이 아닌 코드.
     */
    int run(int argc, char const* argv[]);
};

#endif // VMS_APPLICATION_H
