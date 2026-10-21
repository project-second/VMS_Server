#include "app/VmsCreateAdminCommand.h"

#include "DbRuntime.hpp"
#include "StringHelpers.h"
#include "VmsAdminBootstrap.h"

#include <iostream>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <termios.h>
#include <unistd.h>
#endif

namespace {

// 실행 파일 이름과 create-admin 다음에 오는 --id/--name 옵션을 해석한다.
bool parseCreateAdminArgs(int argc, char const* argv[], std::string& loginId, std::string& name) {
    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        if ((argument == "--id" || argument == "--name") && i + 1 < argc) {
            std::string& destination = argument == "--id" ? loginId : name;
            // 이미 값이 지정된 옵션을 다시 받으면 잘못된 인자로 처리한다.
            if (!destination.empty()) return false;
            destination = argv[++i];
        } else {
            // 지원하지 않는 옵션이나 값이 뒤따르지 않는 옵션은 거부한다.
            return false;
        }
    }
    // 로그인 ID와 이름의 앞뒤 공백을 제거하고 두 필수 값이 모두 있는지 확인한다.
    loginId = trim(loginId);
    name = trim(name);
    return !loginId.empty() && !name.empty();
}

bool readPasswordLine(const char* prompt, std::string& password) {
    // 입력을 기다리기 전에 안내 문구가 표시되도록 출력 버퍼를 비운다.
    std::cerr << prompt << std::flush;

#if defined(__unix__) || defined(__APPLE__)
    termios previous{};
    const bool terminal = isatty(STDIN_FILENO) == 1;
    if (terminal) {
        // 유닉스 계열의 터미널 입력에서는 기존 설정을 저장한 뒤 입력 문자 표시를 끈다.
        if (tcgetattr(STDIN_FILENO, &previous) != 0) return false;
        termios hidden = previous;
        hidden.c_lflag &= static_cast<tcflag_t>(~ECHO);
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden) != 0) return false;
    }
#endif

    // 한 줄 읽기의 성공 여부만 확인하며, 빈 비밀번호 검증은 호출자가 수행한다.
    const bool read = static_cast<bool>(std::getline(std::cin, password));

#if defined(__unix__) || defined(__APPLE__)
    if (terminal) {
        // 입력 실패 시에도 원래 터미널 설정으로 복원한 뒤 줄바꿈을 출력한다.
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &previous);
        std::cerr << '\n';
    }
#endif
    return read;
}

} // namespace

void printVmsUsage(const char* program) {
    std::cerr
        << "Usage:\n"
        << "  " << program << "\n"
        << "  " << program << " create-admin --id <login-id> --name <name>\n";
}

int runCreateAdminCommand(int argc, char const* argv[], const AppRuntimeConfig& config) {
    std::string loginId;
    std::string name;
    // 옵션 검증에 실패하면 비밀번호를 묻기 전에 사용법과 인자 오류 코드를 반환한다.
    if (!parseCreateAdminArgs(argc, argv, loginId, name)) {
        printVmsUsage(argv[0]);
        return 2;
    }

    // 비밀번호는 표준 입력에서 두 번 받아 확인한다.
    std::string password;
    std::string confirmation;
    if (!readPasswordLine("Password: ", password)
        || !readPasswordLine("Confirm password: ", confirmation)) {
        std::cerr << "Failed to read password.\n";
        return 2;
    }
    // 공백뿐인 비밀번호는 거부하되, 확인 입력과의 비교에는 원래 입력값을 사용한다.
    if (trim(password).empty()) {
        std::cerr << "Password must not be empty.\n";
        return 2;
    }
    if (password != confirmation) {
        std::cerr << "Passwords do not match.\n";
        return 2;
    }

    // DB 설정을 적용하고 최초 관리자 생성 정책에 따른 계정 등록을 수행한다.
    AppDb::configure(config.dbConninfo, config.dbMapperPath);
    const auto status = VmsAdminBootstrap{}.createAdmin(loginId, name, password);
    // 생성 결과를 메시지와 종료 코드로 변환해 명령 실행 결과를 구분할 수 있게 한다.
    if (status == VmsAdminBootstrap::Status::Created) {
        std::cout << "Initial administrator '" << loginId << "' created.\n";
        return 0;
    }
    if (status == VmsAdminBootstrap::Status::AdminAlreadyExists) {
        std::cerr << "An administrator already exists; creation refused.\n";
        return 3;
    }
    if (status == VmsAdminBootstrap::Status::LoginIdAlreadyExists) {
        std::cerr << "Login ID already exists.\n";
        return 4;
    }
    if (status == VmsAdminBootstrap::Status::AdminRoleMissing) {
        std::cerr << "ADMIN role is missing; apply database migrations first.\n";
        return 5;
    }
    std::cerr << "Failed to create initial administrator.\n";
    return 1;
}
