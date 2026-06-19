#ifndef SECRET_CODEC_H
#define SECRET_CODEC_H

#include <optional>
#include <string>

// 저장된 secret 값을 애플리케이션에서 사용할 평문으로 변환한다.
// 현재는 키 기반 암호화가 도입되기 전 단계라 `plain:<secret>`와 기존 raw 값을 지원한다.
std::optional<std::string> decode_stored_secret(const std::string& secret);
std::string encode_plain_secret(const std::string& secret);

#endif // SECRET_CODEC_H
