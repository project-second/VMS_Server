#include "ApplicationConfigLoader.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "LogSync.h"

namespace {

// YAML 키 경로, 누락 시 처리 규칙, 값을 대입할 구조체 멤버를 연결하는 항목.
struct Binding {
    const char* path; // app.http.server.port처럼 점으로 구분한 YAML 키 경로.
    bool required; // 기본값이 없을 때 누락 또는 빈 문자열을 오류로 처리할지 여부.
    std::optional<std::string> default_value; // 키가 없거나 null일 때 적용할 기본값.
    // 항목의 타입에 맞는 멤버 포인터 하나를 지정하고 나머지는 nullptr로 둔다.
    std::string AppRuntimeConfig::* string_field;
    int AppRuntimeConfig::* int_field;
    bool AppRuntimeConfig::* bool_field;
};

/**
 * @brief YAML 맵의 스칼라 키를 순회해 이름이 일치하는 자식 노드를 찾는다.
 * @param node 자식 노드를 조회할 YAML 노드.
 * @param key 찾을 자식 키 이름.
 * @return 일치하는 자식 노드. 부모가 유효한 맵이 아니거나 키가 없으면 빈 노드.
 */
YAML::Node findChild(const YAML::Node& node, const std::string& key) {
    if (!node || !node.IsDefined() || node.IsNull() || !node.IsMap()) {
        return YAML::Node{};
    }

    for (YAML::const_iterator it = node.begin(); it != node.end(); ++it) {
        if (it->first.IsScalar() && it->first.as<std::string>() == key) {
            return it->second;
        }
    }

    return YAML::Node{};
}

/**
 * @brief 점으로 구분된 키 경로를 지정한 위치부터 재귀적으로 탐색한다.
 * @param node 현재 경로 요소를 조회할 YAML 노드.
 * @param path 점으로 구분된 전체 YAML 키 경로.
 * @param start path에서 현재 경로 요소가 시작하는 위치.
 * @return 남은 경로에 대응하는 노드. 경로 중 조회할 수 없는 키가 있으면 빈 노드.
 */
YAML::Node findNodePart(const YAML::Node& node, const std::string& path, std::size_t start) {
    // 점을 기준으로 키를 하나씩 분리해 중첩된 YAML 맵을 재귀적으로 탐색한다.
    const std::size_t dot = path.find('.', start);
    const std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
    const YAML::Node child = findChild(node, key);

    if (dot == std::string::npos) {
        return child;
    }

    return findNodePart(child, path, dot + 1);
}

/**
 * @brief 루트 노드에서 점으로 구분된 YAML 키 경로를 조회한다.
 * @param root 탐색을 시작할 YAML 루트 노드.
 * @param dotted_path 조회할 키 경로. nullptr이 아닌 문자열이어야 한다.
 * @return 경로에 대응하는 노드. 경로를 찾을 수 없으면 빈 노드.
 */
YAML::Node findNode(const YAML::Node& root, const char* dotted_path) {
    // app.http.server.port 경로는 app → http → server → port 순서로 조회한다.
    return findNodePart(root, std::string(dotted_path), 0);
}

/**
 * @brief YAML 노드가 정의되지 않았거나 null이어서 값이 누락되었는지 확인한다.
 * @param node 값의 존재 여부를 확인할 YAML 노드.
 * @return 값이 누락되었으면 true, 그 밖의 노드이면 false.
 */
bool missing(const YAML::Node& node) {
    return !node || !node.IsDefined() || node.IsNull();
}

/**
 * @brief 문자열을 10진수 양의 정수로 변환한다.
 * @param value 변환할 문자열. 변환 후 남는 문자가 있으면 실패한다.
 * @param[out] out 성공 시 변환한 값을 저장할 변수. 실패 시 기존 값을 유지한다.
 * @return int 범위의 양의 정수로 변환되면 true, 형식·범위 오류 또는 0 이하의 값이면 false.
 */
bool parseInt(const std::string& value, int& out) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed, 10);
        // 문자열 전체가 정수로 해석되어야 하며, 수치 설정은 모두 양수만 허용한다.
        if (consumed != value.size() || parsed <= 0) {
            return false;
        }
        out = parsed;
        return true;
    } catch (const std::exception&) {
        // 숫자로 변환할 수 없거나 int 범위를 벗어난 입력은 검증 실패로 처리한다.
        return false;
    }
}

/**
 * @brief 소문자 불리언 표기 또는 1/0 문자열을 bool 값으로 변환한다.
 * @param value true/yes/on/1 또는 false/no/off/0 중 하나인 문자열. 대소문자를 구분한다.
 * @param[out] out 성공 시 변환한 값을 저장할 변수. 실패 시 기존 값을 유지한다.
 * @return 허용된 표기로 변환되면 true, 그 밖의 문자열이면 false.
 */
bool parseBool(const std::string& value, bool& out) {
    if (value == "true" || value == "yes" || value == "on" || value == "1") {
        out = true;
        return true;
    }
    if (value == "false" || value == "no" || value == "off" || value == "0") {
        out = false;
        return true;
    }
    return false;
}

/**
 * @brief 바인딩의 멤버 타입에 맞게 값을 검증·변환한 뒤 런타임 설정에 대입한다.
 * @param binding 대상 설정 멤버와 타입, 오류 로그용 YAML 키 경로를 담은 바인딩.
 * @param value 설정 필드에 반영할 문자열 값.
 * @param[in,out] config 변환한 값을 대입할 런타임 설정.
 * @return 값 반영에 성공하면 true, 정수 또는 불리언 변환에 실패하면 false.
 */
bool applyRuntimeField(const Binding& binding, const std::string& value, AppRuntimeConfig& config) {
    if (binding.string_field != nullptr) {
        config.*(binding.string_field) = value;
        return true;
    }

    if (binding.int_field != nullptr) {
        int parsed = 0;
        if (!parseInt(value, parsed)) {
            LogSync::error("[Config] invalid integer key: ", binding.path);
            return false;
        }
        config.*(binding.int_field) = parsed;
    }

    if (binding.bool_field != nullptr) {
        bool parsed = false;
        if (!parseBool(value, parsed)) {
            LogSync::error("[Config] invalid boolean key: ", binding.path);
            return false;
        }
        config.*(binding.bool_field) = parsed;
    }

    return true;
}

/**
 * @brief YAML 설정 항목 하나에 기본값·필수 여부·타입 검증 규칙을 적용한다.
 *
 * 키가 없거나 null이면 바인딩의 기본값을 적용하고,
 * 선택 항목의 빈 문자열은 기존 설정값을 유지한다.
 * @param root 설정값을 조회할 YAML 루트 노드.
 * @param binding 키 경로, 누락 시 처리 규칙 및 대상 설정 멤버를 담은 바인딩.
 * @param[in,out] config 검증된 값을 반영할 런타임 설정.
 * @return 값 반영 또는 선택 항목 건너뛰기에 성공하면 true, 필수 항목·형식 검증에 실패하면 false.
 */
bool applyBinding(const YAML::Node& root, const Binding& binding, AppRuntimeConfig& config) {
    const YAML::Node node = findNode(root, binding.path);

    if (missing(node)) {
        // 키 부재 또는 null에는 바인딩의 기본값을 우선 적용한다.
        if (binding.default_value) {
            return applyRuntimeField(binding, *binding.default_value, config);
        }

        // 기본값도 없는 필수 항목은 오류이며, 선택 항목은 기존 필드 값을 유지한다.
        if (binding.required) {
            LogSync::error("[Config] missing required key: ", binding.path);
            return false;
        }

        return true;
    }

    // 문자열·숫자·불리언 같은 단일 값만 허용하고 맵이나 목록은 거부한다.
    if (!node.IsScalar()) {
        LogSync::error("[Config] invalid key (scalar required): ", binding.path);
        return false;
    }

    const std::string value = node.as<std::string>();
    if (value.empty()) {
        // 빈 문자열인 필수 항목은 오류이며, 선택 항목은 기본값 적용 없이 기존 값을 유지한다.
        if (binding.required) {
            LogSync::error("[Config] empty required key: ", binding.path);
            return false;
        }
        return true;
    }

    return applyRuntimeField(binding, value, config);
}

/**
 * @brief YAML 키와 런타임 설정 멤버를 연결하는 전체 바인딩 목록을 반환한다.
 * @return 키 경로, 필수 여부, 기본값 및 대상 멤버를 담은 정적 바인딩 목록의 상수 참조.
 */
const std::vector<Binding>& bindings() {
    // YAML 키와 AppRuntimeConfig 멤버의 대응표. 기본값은 구조체의 초기값과 함께 관리한다.
    static const std::vector<Binding> items = {
        // DB 접속 문자열만 필수이며, 나머지 항목은 누락 시 지정된 기본값을 사용한다.
        {"app.db.conninfo", true, std::nullopt, &AppRuntimeConfig::dbConninfo, nullptr, nullptr},
        {"app.db.mapperPath", false, "resources/db", &AppRuntimeConfig::dbMapperPath, nullptr, nullptr},

        {"app.http.server.host", false, "0.0.0.0", &AppRuntimeConfig::serverHost, nullptr, nullptr},
        {"app.http.server.port", false, "8080", nullptr, &AppRuntimeConfig::serverPort, nullptr},
        {"app.http.server.threads", false, "8", nullptr, &AppRuntimeConfig::serverThreads, nullptr},
        {"app.http.clientPool.threads", false, "8", nullptr, &AppRuntimeConfig::clientPoolThreads, nullptr},
        {"app.http.clientPool.maxQueue", false, "500", nullptr, &AppRuntimeConfig::clientPoolMaxQueue, nullptr},

        {"app.onvif.discovery.intervalMs", false, "300000", nullptr,
            &AppRuntimeConfig::onvifDiscoveryIntervalMs, nullptr},
        {"app.onvif.soapLog.enabled", false, "false", nullptr, nullptr,
            &AppRuntimeConfig::onvifSoapLogEnabled},
        {"app.onvif.soapLog.dir", false, "/tmp", &AppRuntimeConfig::onvifSoapLogDir, nullptr, nullptr},
        {"app.media.recording.rootDir", false, "recordings/onvif", &AppRuntimeConfig::mediaRecordingRootDir,
            nullptr, nullptr},
        {"app.media.recording.splitSeconds", false, "5", nullptr,
            &AppRuntimeConfig::mediaRecordingSplitSeconds, nullptr},
        {"app.media.rtspRelay.enabled", false, "true", nullptr, nullptr,
            &AppRuntimeConfig::mediaRtspRelayEnabled},
        {"app.media.rtspRelay.bindAddress", false, "0.0.0.0",
            &AppRuntimeConfig::mediaRtspRelayBindAddress, nullptr, nullptr},
        {"app.media.rtspRelay.port", false, "8555", nullptr,
            &AppRuntimeConfig::mediaRtspRelayPort, nullptr},
        {"app.media.rtspRelay.advertisedHost", false, "127.0.0.1",
            &AppRuntimeConfig::mediaRtspRelayAdvertisedHost, nullptr, nullptr},

        {"app.mediamtx.playbackSyncIntervalMs", false, "30000", nullptr,
            &AppRuntimeConfig::mediaMtxPlaybackSyncIntervalMs, nullptr},
    };

    return items;
}

} // namespace

/**
 * @brief YAML 설정 파일을 읽고 각 바인딩의 값을 검증해 런타임 설정에 반영한다.
 * @param[in,out] config 값을 반영할 설정. 실패하더라도 이미 반영된 일부 항목은 변경된 상태로 남을 수 있다.
 * @param directory 설정 파일 디렉터리. 상대 경로는 현재 작업 디렉터리를 기준으로 한다.
 * @param file 읽을 YAML 파일 이름.
 * @return 파일 로딩과 모든 설정 검증에 성공하면 true, 파일이 없거나 로딩·검증에 실패하면 false.
 * @throws std::filesystem::filesystem_error 파일 경로 계산 또는 존재 여부 확인 중 파일 시스템 오류가 발생한 경우.
 */
bool loadApplicationConfig(AppRuntimeConfig& config, const std::string& directory, const std::string& file) {
    // 상대 디렉터리는 현재 작업 디렉터리 기준으로 계산하며 절대 디렉터리는 그대로 사용한다.
    const std::filesystem::path path{std::filesystem::current_path() / directory / file};

    if (!std::filesystem::exists(path)) {
        LogSync::error("[Config] ", path.string(), " not found.");
        return false;
    }

    try {
        const YAML::Node root = YAML::LoadFile(path.string());
        // 설정 항목을 읽기 전에 최상위 app 노드의 존재 여부를 확인한다.
        if (missing(findNode(root, "app"))) {
            LogSync::error("[Config] missing required key: app");
            return false;
        }

        bool ok = true;
        for (const Binding& binding : bindings()) {
            // applyBinding을 먼저 호출해 앞선 항목이 실패했어도 나머지 항목의 오류를 검사한다.
            // 성공한 항목은 즉시 반영되며, 전체 로딩 실패 시에도 되돌리지 않는다.
            ok = applyBinding(root, binding, config) && ok;
        }

        if (!ok) return false;

        LogSync::info("[Config] loaded: ", path.string());
        return true;
    } catch (const std::exception& e) {
        // YAML 파싱이나 로딩·값 변환 중 발생한 예외를 로그와 실패 반환값으로 전달한다.
        LogSync::error("[Config] failed to load application.yml: ", e.what());
        return false;
    }
}
