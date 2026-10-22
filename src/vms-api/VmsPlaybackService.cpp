/**
 * @file VmsPlaybackService.cpp
 * @brief 녹화 메타데이터 반영, HLS VOD 생성 및 세션 토큰 검증 구현.
 */
#include "VmsPlaybackService.h"

#include "LogSync.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#include <openssl/crypto.h>
#include <openssl/rand.h>

namespace {

/**
 * @brief Unix epoch 기준 밀리초 값을 시스템 시계의 시각으로 변환한다.
 * @param value Unix epoch 기준 밀리초 단위 시각.
 * @return 해당 시각을 나타내는 system_clock::time_point.
 */
std::chrono::system_clock::time_point fromMs(std::int64_t value) {
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(value));
}

/**
 * @brief 시스템 시계의 시각을 Unix epoch 기준 밀리초 값으로 변환한다.
 * @param value 변환할 시스템 시계 시각.
 * @return 밀리초 단위 정수 시각. 밀리초보다 작은 단위는 버린다.
 */
std::int64_t toMs(const std::chrono::system_clock::time_point& value) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        value.time_since_epoch()).count();
}

/**
 * @brief 종료 시각이 있는 DB 레코드를 재생 구간 계산용 녹화 세그먼트로 변환한다.
 * @param rows 저장소에서 조회한 녹화 세그먼트 레코드 목록.
 * @param cameraId 모든 변환 항목에 부여할 논리 카메라 식별자.
 * @return 입력 순서를 유지한 변환 목록. 종료 시각이 없는 항목은 제외한다.
 */
std::vector<RecordingSegment> toRecordingSegments(
    const std::vector<PlaybackSegmentRecord>& rows,
    const std::string& cameraId) {
    std::vector<RecordingSegment> result;
    result.reserve(rows.size());
    for (const auto& row : rows) {
        if (!row.endMs) continue;
        RecordingSegment segment;
        segment.profile_id = row.profileId;
        segment.camera_id = cameraId;
        segment.file_path = row.filePath;
        segment.started_at = fromMs(row.startMs);
        segment.ended_at = fromMs(*row.endMs);
        segment.status = row.status;
        result.push_back(std::move(segment));
    }
    return result;
}

} // namespace

// 저장소는 참조로 연결하고, HLS 세션 관리자가 캐시 루트 경로를 보관하도록 구성한다.
VmsPlaybackService::VmsPlaybackService(
    VmsPlaybackRepository& repository,
    std::filesystem::path cacheRoot)
    : repository_(repository), sessionManager_(std::move(cacheRoot)) {}

// 활성 프로파일 선택과 대상 정보 조회는 저장소의 매퍼 정책에 위임한다.
std::optional<PlaybackTargetRecord> VmsPlaybackService::findTarget(
    std::int64_t deviceId, std::int64_t channelId) const {
    return repository_.findTarget(deviceId, channelId);
}

// API의 1부터 시작하는 페이지 번호를 저장소의 LIMIT/OFFSET 인자로 변환한다.
PlaybackSegmentPage VmsPlaybackService::findPage(
    std::int64_t profileId, std::int64_t page, std::int64_t size) const {
    return repository_.findPage(profileId, size, (page - 1) * size);
}

// 시간 구간의 겹침 판정과 정렬은 저장소에 위임하고 조회 결과를 그대로 반환한다.
std::vector<PlaybackSegmentRecord> VmsPlaybackService::findRange(
    std::int64_t profileId, std::int64_t fromMsValue, std::int64_t toMsValue) const {
    return repository_.findRange(profileId, fromMsValue, toMsValue);
}

// 세그먼트의 상태 제한 없이 장치·채널 정보가 포함된 단건 조회 결과를 반환한다.
std::optional<PlaybackSegmentRecord> VmsPlaybackService::findSegment(
    std::int64_t segmentId) const {
    return repository_.findById(segmentId);
}

// 녹화 상태에 따라 시작 정보 등록·갱신 또는 종료 정보 갱신으로 나누어 DB에 반영한다.
bool VmsPlaybackService::persistRecordingSegment(const RecordingSegment& segment) const {
    const auto startedMs = toMs(segment.started_at);
    // 진행 중인 녹화는 종료 시각 없이 시작 메타데이터만 저장한다.
    if (segment.status == "recording") {
        return repository_.saveStarted(
            segment.profile_id, segment.file_path, startedMs, "H264");
    }
    // 종료 상태는 종료 시각이 필요하며, 파일 크기 조회 오류는 0바이트로 기록한다.
    if (!segment.ended_at) return false;
    std::error_code error;
    const auto size = std::filesystem::file_size(segment.file_path, error);
    return repository_.saveClosed(
        segment.file_path, toMs(*segment.ended_at),
        error ? 0 : static_cast<std::int64_t>(size), "H264", segment.status);
}

// 서버 기동 시 이전 실행에서 recording 상태로 남은 메타데이터의 일괄 정리를 요청한다.
bool VmsPlaybackService::interruptStaleRecordings() const {
    return repository_.interruptStaleRecordings();
}

// 녹화 구간 조회부터 HLS 파일 생성, 응답용 URL 구성 및 접근 가능한 세션 등록까지 조율한다.
PlaybackHlsResult VmsPlaybackService::createHls(
    const PlaybackTargetRecord& target,
    std::int64_t fromMsValue, std::int64_t toMsValue) {
    PlaybackHlsResult result;
    // HLS 변환 요청은 시작 시각이 종료보다 빠르고 전체 길이가 1시간 이하인 경우만 허용한다.
    if (fromMsValue >= toMsValue || toMsValue - fromMsValue > 60LL * 60LL * 1000LL) {
        result.errorMessage = "playback range must be greater than 0 and at most 1 hour";
        return result;
    }

    // 프로파일 ID를 논리 카메라 ID로 사용하고, DB 레코드를 재생 구간 계산용 세그먼트로 바꾼다.
    const std::string cameraId = "profile_" + std::to_string(target.profileId);
    PlaybackRequest request{cameraId, fromMs(fromMsValue), fromMs(toMsValue)};
    const auto rows = repository_.findRange(target.profileId, fromMsValue, toMsValue);
    auto playback = playbackService_.create_session(
        request, toRecordingSegments(rows, cameraId));
    // 재생 구간 구성 실패는 API에서 대상 부재로 응답할 수 있도록 별도 표시한다.
    if (!playback.ok || !playback.session) {
        result.notFound = true;
        result.errorMessage = playback.error_message;
        return result;
    }

    // 캐시와 출력 계획을 만든 뒤 실제 세그먼트를 변환하고 재생목록 파일을 생성한다.
    auto hls = sessionManager_.create_session(*playback.session);
    if (!hls.ok || !hls.session) {
        result.errorMessage = hls.error_message;
        return result;
    }
    auto converted = converter_.convert_all_segments(*hls.session);
    if (!converted.ok) {
        result.errorMessage = converted.error_message;
        return result;
    }
    auto playlist = playlistBuilder_.write_playlist(*hls.session);
    if (!playlist.ok) {
        result.errorMessage = playlist.error_message;
        return result;
    }

    // HLS 생성이 완료된 세션에 접근 토큰을 발급하고, 난수 생성 실패는 결과 메시지로 전달한다.
    std::string accessToken;
    try {
        accessToken = createAccessToken();
    } catch (const std::exception& error) {
        result.errorMessage = error.what();
        return result;
    }

    // 생성된 재생목록을 읽어 HTTP 응답으로 반환할 토큰 포함 텍스트를 구성한다.
    std::ifstream input(hls.session->playlist_path);
    if (!input.is_open()) {
        result.errorMessage = "failed to read generated HLS playlist";
        return result;
    }
    std::ostringstream rewritten;
    std::string line;
    while (std::getline(input, line)) {
        const auto found = std::find_if(
            hls.session->segments.begin(), hls.session->segments.end(),
            [&line](const HlsVodSegment& segment) {
                return segment.output_file_name == line;
            });
        // 등록된 세그먼트 파일명과 정확히 일치하는 줄만 토큰을 붙인 API URL로 바꾼다.
        if (found != hls.session->segments.end()) {
            rewritten << "/playback/sessions/" << hls.session->session_id
                      << "/segments/" << found->output_file_name
                      << "?token=" << accessToken << "\n";
        } else {
            rewritten << line << "\n";
        }
    }

    // 응답 준비를 마친 세션만 등록하며, 기존 만료 세션은 등록 직전에 정리한다.
    cleanupExpiredSessions();
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessions_[hls.session->session_id] = AuthorizedHlsSession{
            *hls.session,
            std::move(accessToken),
            std::chrono::steady_clock::now()};
    }
    result.ok = true;
    result.sessionId = hls.session->session_id;
    result.playlist = rewritten.str();
    return result;
}

// 32바이트 난수를 각 바이트당 두 자리의 소문자 16진수로 인코딩한다.
std::string VmsPlaybackService::createAccessToken() {
    std::array<unsigned char, 32> bytes{};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
        throw std::runtime_error("failed to generate HLS session access token");
    }
    std::ostringstream token;
    token << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        token << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return token.str();
}

// 길이와 빈 값을 먼저 검사한 뒤 토큰 내용 비교를 OpenSSL에 위임한다.
bool VmsPlaybackService::tokensEqual(
    const std::string& lhs, const std::string& rhs) {
    return lhs.size() == rhs.size() && !lhs.empty() &&
        CRYPTO_memcmp(lhs.data(), rhs.data(), lhs.size()) == 0;
}

// 만료 세션을 정리한 뒤 세션 토큰과 파일명 등록 여부를 검사해 세그먼트 파일을 읽는다.
PlaybackBinaryResult VmsPlaybackService::readHlsSegment(
    const std::string& sessionId,
    const std::string& fileName,
    const std::string& accessToken) {
    cleanupExpiredSessions();
    std::filesystem::path path;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        const auto it = sessions_.find(sessionId);
        if (it == sessions_.end() ||
            !tokensEqual(it->second.accessToken, accessToken)) {
            return {false, true, "invalid or expired HLS session token", {}};
        }
        // 요청 파일명은 세션에 등록된 출력 파일명과 정확히 일치하는 항목으로 제한한다.
        const auto segment = std::find_if(
            it->second.session.segments.begin(), it->second.session.segments.end(),
            [&fileName](const HlsVodSegment& item) {
                return item.output_file_name == fileName;
            });
        if (segment == it->second.session.segments.end()) {
            return {false, false, "HLS segment not found", {}};
        }
        // 토큰·파일명 검증을 통과한 시점부터 TTL을 다시 계산한다.
        // 접근 시각은 실제 파일을 열기 전에 갱신한다.
        it->second.lastAccessAt = std::chrono::steady_clock::now();
        path = segment->output_file_path;
    }
    // 파일 I/O 동안 세션 맵 접근을 막지 않도록 경로를 복사한 후 잠금을 해제한 상태에서 읽는다.
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {false, false, "HLS segment file not found", {}};
    }
    std::ostringstream body;
    body << input.rdbuf();
    return {true, false, {}, body.str()};
}

// 마지막 접근이 TTL 기준보다 오래된 세션의 캐시와 메모리 등록 정보를 정리한다.
void VmsPlaybackService::cleanupExpiredSessions() {
    const auto threshold = std::chrono::steady_clock::now() - sessionTtl_;
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second.lastAccessAt < threshold) {
            std::error_code error;
            // 캐시 삭제 오류는 error_code로 받고 세션 등록은 제거해 이후 접근을 차단한다.
            std::filesystem::remove_all(it->second.session.cache_dir, error);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}
