#include "EnvManager.h"

bool readEnvIntStrict(const char* key, int& out) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || *raw == '\0') return false;

    char* end = nullptr;
    const long parsed = std::strtol(raw, &end, 10);
    if (end == raw || *end != '\0' || parsed <= 0) return false;
    out = static_cast<int>(parsed);
    return true;
}

std::optional<std::string> readEnvStringStrict(const char* key) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || *raw == '\0') return std::nullopt;
    return std::string(raw);
}
