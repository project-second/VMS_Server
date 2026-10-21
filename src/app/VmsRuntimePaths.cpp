#include "app/VmsRuntimePaths.h"

#include <cstdlib>
#include <string>

namespace {

// 기존 검색 경로를 유지하면서 지정한 디렉터리가 먼저 검색되도록 환경 변수 앞에 추가한다.
void prependEnvironmentPath(const char* name, const std::filesystem::path& path) {
    std::string value = path.string();
    const char* current = std::getenv(name);
    if (current != nullptr && *current != '\0') {
        // 경로 목록은 유닉스 환경 변수 형식에 맞춰 콜론으로 구분한다.
        value += ":";
        value += current;
    }
    setenv(name, value.c_str(), 1);
}

} // namespace

VmsRuntimePaths resolveRuntimePaths(const char* argv0) {
    namespace fs = std::filesystem;
    // 전달받은 실행 파일 경로를 절대 경로로 바꾼 뒤 파일명을 제외한 디렉터리를 구한다.
    const fs::path executableDirectory = fs::absolute(argv0 == nullptr ? "" : argv0).parent_path();
    
    // bin과 resources가 같은 설치 루트 아래에 있는 배포 구조를 기준으로 한다.
    // lexically_normal()은 경로의 . 및 .. 요소를 정리하며 실제 파일 존재 여부는 검사하지 않는다.
    return VmsRuntimePaths{
        .executableDirectory = executableDirectory,
        .resourceDirectory = (executableDirectory / ".." / "resources").lexically_normal(),
    };
}

void normalizeRuntimeConfigPaths(AppRuntimeConfig& config, const VmsRuntimePaths& paths) {
    namespace fs = std::filesystem;
    // 상대 경로의 기준을 실행 파일 위치로 고정해 작업 디렉터리에 따른 경로 차이를 없앤다.
    if (fs::path(config.dbMapperPath).is_relative()) {
        config.dbMapperPath = (paths.executableDirectory / config.dbMapperPath).lexically_normal().string();
    }
}

void configurePackagedGStreamerEnvironment(const VmsRuntimePaths& paths) {
    // 실행 파일이 설치 루트의 bin에 있다고 보고 패키지 내부 구성 요소의 위치를 계산한다.
    const std::filesystem::path installRoot = paths.executableDirectory.parent_path();
    const std::filesystem::path libraryPath = installRoot / "lib";
    const std::filesystem::path pluginPath = installRoot / "lib" / "gstreamer-1.0";
    const std::filesystem::path scannerPath = installRoot / "libexec" / "gstreamer-1.0" / "gst-plugin-scanner";

    // gst-plugin-scanner는 별도 process로 실행되므로 vms_server의 RPATH를 상속하지 않는다.
    // child loader가 패키지 내부 GStreamer shared library를 찾도록 환경에 lib 경로를 전달한다.
    if (std::filesystem::exists(libraryPath)) {
        prependEnvironmentPath("LD_LIBRARY_PATH", libraryPath);
    }
    // 플러그인 디렉터리가 있을 때 두 검색 경로 환경 변수에 패키지 경로를 지정한다.
    if (std::filesystem::exists(pluginPath)) {
        setenv("GST_PLUGIN_PATH", pluginPath.c_str(), 1);
        setenv("GST_PLUGIN_PATH_1_0", pluginPath.c_str(), 1);
    }
    // 패키지에 스캐너 실행 파일이 있으면 플러그인 검사에 해당 파일을 사용하도록 지정한다.
    if (std::filesystem::exists(scannerPath)) {
        setenv("GST_PLUGIN_SCANNER", scannerPath.c_str(), 1);
    }
}
