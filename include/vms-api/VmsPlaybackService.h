/**
 * @file VmsPlaybackService.h
 * @brief 녹화 메타데이터 조회·저장과 토큰 기반 HLS VOD 세션 관리 인터페이스.
 */
#pragma once

#include "HlsVodConverter.h"
#include "HlsVodPlaylistBuilder.h"
#include "HlsVodSession.h"
#include "RecordingSegmentStore.h"
#include "VmsPlaybackRepository.h"

#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief HLS VOD 생성 결과와 응답용 재생목록.
 */
struct PlaybackHlsResult {
    bool ok = false; ///< HLS 변환과 접근 토큰을 포함한 재생목록 구성의 성공 여부.
    bool notFound = false; ///< 재생 구간 구성 실패를 대상 부재로 분류했는지 여부.
    std::string errorMessage; ///< 실패 원인을 설명하는 메시지.
    std::string sessionId; ///< 성공 시 생성된 HLS 세션 식별자.
    std::string playlist; ///< 성공 시 세그먼트 URL에 접근 토큰을 붙인 재생목록 텍스트.
};

/**
 * @brief HLS 세그먼트 읽기의 성공 여부, 인증 오류 분류 및 바이너리 본문.
 */
struct PlaybackBinaryResult {
    bool ok = false; ///< 세그먼트 파일을 열어 바이너리 응답을 구성했는지 여부.
    bool unauthorized = false; ///< 세션 부재·만료 또는 토큰 불일치로 접근이 거부되었는지 여부.
    std::string errorMessage; ///< 실패 원인을 설명하는 메시지.
    std::string body; ///< 성공 시 파일에서 읽은 세그먼트 바이너리 데이터.
};

/**
 * @brief 녹화 메타데이터를 관리하고 과거 영상 구간을 토큰으로 접근하는 HLS VOD로 제공한다.
 *
 * 메타데이터는 Repository에 위임하고, HLS 변환·캐시 생성·재생목록 구성을 조율한다.
 * 생성된 세션의 토큰과 마지막 접근 시각은 메모리에 보관하며 세션 맵 접근을 뮤텍스로 보호한다.
 */
class VmsPlaybackService {
public:
    /**
     * @brief 녹화 메타데이터 저장소와 HLS 캐시 루트로 재생 서비스를 구성한다.
     *
     * repository는 외부에서 소유하며 서비스보다 오래 유지되어야 한다.
     * @param repository 재생 대상과 녹화 세그먼트 메타데이터를 조회·저장할 저장소.
     * @param cacheRoot HLS 세션별 캐시 디렉터리를 만들 루트 경로.
     */
    VmsPlaybackService(
        VmsPlaybackRepository& repository,
        std::filesystem::path cacheRoot);

    /**
     * @brief 장치·채널의 재생용 활성 프로파일과 표시 정보를 조회한다.
     * @param deviceId 조회할 장치 DB 식별자.
     * @param channelId 장치에 속한 채널 DB 식별자.
     * @return 재생 대상. 대상 부재 또는 저장소 조회·변환 실패 시 std::nullopt.
     */
    std::optional<PlaybackTargetRecord> findTarget(
        std::int64_t deviceId, std::int64_t channelId) const;

    /**
     * @brief 프로파일의 종료된 녹화 세그먼트를 페이지 번호 기준으로 조회한다.
     *
     * 페이지 번호를 `(page - 1) * size` 오프셋으로 변환해 저장소에 전달한다.
     * @param profileId 조회할 프로파일 DB 식별자.
     * @param page 1부터 시작하는 페이지 번호. 유효성 검증은 호출자가 수행한다.
     * @param size 페이지당 최대 항목 수. 유효성 검증은 호출자가 수행한다.
     * @return 시작 시각 내림차순의 현재 페이지 항목과 전체 조회 건수.
     */
    PlaybackSegmentPage findPage(
        std::int64_t profileId, std::int64_t page, std::int64_t size) const;

    /**
     * @brief 요청 시간 구간과 겹치는 종료된 녹화 세그먼트의 메타데이터를 조회한다.
     * @param profileId 조회할 프로파일 DB 식별자.
     * @param fromMs 조회 구간의 시작 경계(Unix epoch 기준 밀리초).
     * @param toMs 조회 구간의 종료 경계(Unix epoch 기준 밀리초).
     * @return 시작 시각·ID 오름차순의 세그먼트 목록. 대상 부재 또는 DB 조회 실패 시 빈 목록.
     */
    std::vector<PlaybackSegmentRecord> findRange(
        std::int64_t profileId, std::int64_t fromMs, std::int64_t toMs) const;

    /**
     * @brief 상태 제한 없이 세그먼트와 연결된 장치·채널 정보를 단건 조회한다.
     * @param segmentId 조회할 녹화 세그먼트 DB 식별자.
     * @return 녹화 세그먼트. 대상 부재 또는 저장소 조회·변환 실패 시 std::nullopt.
     */
    std::optional<PlaybackSegmentRecord> findSegment(std::int64_t segmentId) const;

    /**
     * @brief 녹화 상태에 따라 시작 또는 종료 메타데이터를 DB에 반영한다.
     *
     * recording이면 시작 정보를 등록·갱신하고, 그 외 상태이면 종료 시각이 있을 때
     * 종료 정보를 갱신한다. 코덱은 H264로 저장하며 파일 크기 조회 실패 시 크기는 0으로 기록한다.
     * @param segment DB에 반영할 녹화 세그먼트.
     * @return SQL 실행에 성공하면 true, 종료 시각 누락 또는 DB 실행 실패 처리 시 false.
     */
    bool persistRecordingSegment(const RecordingSegment& segment) const;

    /**
     * @brief recording 상태로 남은 모든 세그먼트의 중단 처리를 저장소에 요청한다.
     * @return SQL 실행에 성공하면 true, DB 실행 실패 처리 시 false.
     */
    bool interruptStaleRecordings() const;

    /**
     * @brief 요청 구간을 HLS VOD로 변환하고 접근 토큰이 포함된 재생목록을 반환한다.
     *
     * 0보다 크고 1시간 이하인 구간을 허용한다. 녹화 구간 구성, 캐시 생성, 변환 및
     * 재생목록 파일 생성을 마친 뒤 접근 토큰을 발급한다.
     * 응답용 재생목록을 구성한 후 세션을 메모리에 등록한다.
     * @param target 재생할 프로파일 식별자를 포함한 재생 대상.
     * @param fromMs 요청 시작 시각(Unix epoch 기준 밀리초).
     * @param toMs 요청 종료 시각(Unix epoch 기준 밀리초).
     * @return 성공 시 세션 ID와 토큰이 포함된 재생목록, 실패 시 오류 설명과 대상 부재 분류.
     */
    PlaybackHlsResult createHls(
        const PlaybackTargetRecord& target,
        std::int64_t fromMs, std::int64_t toMs);

    /**
     * @brief 세션 토큰과 등록된 파일명을 검증하고 HLS 세그먼트의 바이너리 데이터를 읽는다.
     *
     * 만료 세션을 먼저 정리한다. 토큰·파일명 검증을 통과하면 파일을 열기 전에
     * 마지막 접근 시각을 갱신해 세션 유효기간을 연장한다.
     * @param sessionId HLS 세션 식별자.
     * @param fileName 해당 세션에 등록된 출력 세그먼트 파일명.
     * @param accessToken 세션 생성 시 재생목록의 URL에 포함한 접근 토큰.
     * @return 성공 시 바이너리 본문. 세션·토큰 검증 실패 시 unauthorized가 true인 실패 결과.
     */
    PlaybackBinaryResult readHlsSegment(
        const std::string& sessionId,
        const std::string& fileName,
        const std::string& accessToken);

private:
    /**
     * @brief HLS 세션 메타데이터와 접근 토큰, 유효기간 계산에 사용할 마지막 접근 시각.
     */
    struct AuthorizedHlsSession {
        HlsVodSession session; ///< 캐시 경로와 출력 세그먼트 목록을 포함한 세션 정보.
        std::string accessToken; ///< 세그먼트 접근에 사용할 세션별 토큰.
        std::chrono::steady_clock::time_point lastAccessAt; ///< 세션 등록 또는 마지막 토큰·파일명 검증 통과 시각.
    };

    /**
     * @brief 세션 접근에 사용할 32바이트 난수를 16진수 문자열로 생성한다.
     * @return 64자리 소문자 16진수 접근 토큰.
     * @throws std::runtime_error OpenSSL 난수 생성에 실패한 경우.
     */
    static std::string createAccessToken();

    /**
     * @brief 길이와 빈 값 검사를 통과한 토큰의 내용을 CRYPTO_memcmp()로 비교한다.
     * @param lhs 비교할 첫 번째 토큰.
     * @param rhs 비교할 두 번째 토큰.
     * @return 두 토큰이 비어 있지 않고 길이와 내용이 같으면 true, 그 밖에는 false.
     */
    static bool tokensEqual(const std::string& lhs, const std::string& rhs);

    /**
     * @brief 마지막 접근 시각이 TTL 기준보다 오래된 세션의 캐시와 접근 정보를 정리한다.
     *
     * 새 HLS 세션 등록 직전과 세그먼트 읽기 요청 시 호출한다.
     * 캐시 삭제 오류는 error_code로 받고, 해당 세션의 메모리 정보는 제거한다.
     */
    void cleanupExpiredSessions();

    VmsPlaybackRepository& repository_; ///< 외부에서 소유하는 녹화 메타데이터 저장소 참조.
    PlaybackService playbackService_; ///< 요청 구간과 녹화 세그먼트로 재생 구간을 구성하는 서비스.
    HlsVodSessionManager sessionManager_; ///< HLS 캐시 디렉터리와 출력 세그먼트 계획 생성기.
    HlsVodConverter converter_; ///< 원본 녹화 파일의 요청 구간을 HLS 세그먼트로 변환하는 구성 요소.
    HlsVodPlaylistBuilder playlistBuilder_; ///< 변환된 세그먼트의 HLS 재생목록 파일 생성기.
    std::mutex sessionsMutex_; ///< 세션 맵 조회·등록·접근 시각 갱신·삭제를 보호하는 뮤텍스.
    std::unordered_map<std::string, AuthorizedHlsSession> sessions_; ///< 세션 ID별 접근 토큰과 캐시 정보.
    std::chrono::minutes sessionTtl_{15}; ///< 세션 등록 또는 마지막 유효 요청 이후의 비활성 유효기간.
};
