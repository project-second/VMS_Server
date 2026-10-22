/**
 * @file VmsPlaybackRepository.h
 * @brief 재생 대상과 녹화 세그먼트 메타데이터의 조회·저장 인터페이스.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief 장치·채널에 대해 선택된 재생용 프로파일과 표시 정보.
 */
struct PlaybackTargetRecord {
    std::int64_t deviceId = 0; ///< 장치 DB 식별자.
    std::int64_t channelId = 0; ///< 장치에 속한 채널 DB 식별자.
    std::int64_t profileId = 0; ///< 선택된 재생용 프로파일 DB 식별자.
    std::string deviceName; ///< 장치 표시 이름.
    std::string channelName; ///< 채널 표시 이름.
};

/**
 * @brief 녹화 세그먼트의 파일, 시간 범위, 상태 및 연결된 장치 정보.
 *
 * 장치·채널 정보는 VmsPlaybackRepository::findById()의 조인 결과에서 채워지며,
 * 페이지·구간 조회에서는 해당 필드의 기본값을 유지한다.
 */
struct PlaybackSegmentRecord {
    std::int64_t segmentId = 0; ///< 녹화 세그먼트 DB 식별자.
    std::int64_t profileId = 0; ///< 녹화에 사용한 프로파일 DB 식별자.
    std::string filePath; ///< 녹화 파일 경로.
    std::int64_t startMs = 0; ///< 녹화 시작 시각(Unix epoch 기준 밀리초).
    std::optional<std::int64_t> endMs; ///< 녹화 종료 시각(Unix epoch 기준 밀리초). 값이 없으면 std::nullopt.
    std::int64_t fileSize = 0; ///< 파일 크기(바이트). 컬럼이 없거나 NULL이면 기본값 0을 유지한다.
    std::string codec; ///< 녹화 영상 코덱 이름.
    std::string status; ///< 녹화 상태(예: recording, completed, interrupted).
    bool hasGap = false; ///< 녹화 공백 표시 플래그.
    std::int64_t deviceId = 0; ///< 녹화 장치 DB 식별자.
    std::int64_t channelId = 0; ///< 녹화 채널 DB 식별자.
    std::string deviceName; ///< 녹화 장치 표시 이름.
    std::string channelName; ///< 녹화 채널 표시 이름.
};

/**
 * @brief 녹화 세그먼트 한 페이지와 페이지 제한 적용 전 전체 조회 건수.
 */
struct PlaybackSegmentPage {
    std::vector<PlaybackSegmentRecord> items; ///< 현재 페이지에서 변환에 성공한 세그먼트 목록.
    std::int64_t totalCount = 0; ///< 해당 프로파일의 조회 조건에 맞는 전체 행 수.
};

/**
 * @brief playback SQL 매퍼를 통해 재생 대상과 녹화 세그먼트 메타데이터에 접근한다.
 *
 * AppDb의 공통 DB 연결을 사용한다. 조회의 빈 결과만으로 대상 부재와 DB 실행 실패를
 * 구분할 수 없으며, 저장 함수의 bool 반환값은 영향받은 행 수가 아닌 SQL 실행 성공 여부이다.
 */
class VmsPlaybackRepository {
public:
    /**
     * @brief 장치·채널에서 사용할 활성 재생 프로파일과 표시 정보를 조회한다.
     *
     * 활성 채널의 활성 프로파일 중 채널의 use_profile을 우선 선택한다.
     * 해당 프로파일이 없으면 프로파일 번호·ID 순으로 첫 번째 활성 프로파일을 선택한다.
     * @param deviceId 조회할 장치 DB 식별자.
     * @param channelId 장치에 속한 채널 DB 식별자.
     * @return 선택된 재생 대상. 대상 부재, DB 조회 실패 또는 필수 식별자 변환 실패 시 std::nullopt.
     */
    std::optional<PlaybackTargetRecord> findTarget(
        std::int64_t deviceId, std::int64_t channelId) const;

    /**
     * @brief 프로파일의 종료된 녹화 세그먼트를 시작 시각 내림차순으로 페이지 조회한다.
     *
     * 종료 시각이 있는 completed/interrupted 항목을 대상으로 하며,
     * 필수 컬럼을 변환할 수 없는 행은 반환 목록에서 제외한다.
     * @param profileId 조회할 프로파일 DB 식별자.
     * @param limit 가져올 최대 행 수.
     * @param offset 앞에서 건너뛸 행 수.
     * @return 현재 페이지의 세그먼트 목록과 페이지 제한 적용 전 전체 조회 건수.
     */
    PlaybackSegmentPage findPage(
        std::int64_t profileId, std::int64_t limit, std::int64_t offset) const;

    /**
     * @brief 요청 시간 구간과 겹치는 종료된 녹화 세그먼트를 조회한다.
     *
     * 종료 시각이 있는 completed/interrupted 항목 중 `startMs < toMs`이고
     * `endMs > fromMs`인 항목을 시작 시각·세그먼트 ID 오름차순으로 반환한다.
     * @param profileId 조회할 프로파일 DB 식별자.
     * @param fromMs 조회 구간의 시작 경계(Unix epoch 기준 밀리초).
     * @param toMs 조회 구간의 종료 경계(Unix epoch 기준 밀리초).
     * @return 변환에 성공한 세그먼트 목록. 대상 부재 또는 DB 조회 실패 시 빈 목록.
     */
    std::vector<PlaybackSegmentRecord> findRange(
        std::int64_t profileId, std::int64_t fromMs, std::int64_t toMs) const;

    /**
     * @brief 상태 제한 없이 세그먼트와 연결된 장치·채널 정보를 단건 조회한다.
     * @param segmentId 조회할 녹화 세그먼트 DB 식별자.
     * @return 조회된 세그먼트. 대상 부재, DB 조회 실패 또는 필수 컬럼 변환 실패 시 std::nullopt.
     */
    std::optional<PlaybackSegmentRecord> findById(std::int64_t segmentId) const;

    /**
     * @brief 녹화 시작 메타데이터를 파일 경로 기준으로 등록하거나 갱신한다.
     *
     * 상태를 recording으로 저장한다. 같은 파일 경로의 기존 행은 시작 정보를 갱신하고,
     * 종료 시각·파일 크기를 NULL, 공백 표시를 false로 초기화한다.
     * @param profileId 녹화에 사용한 프로파일 DB 식별자.
     * @param filePath 녹화 파일 경로이자 중복 여부를 판단할 키.
     * @param startMs 녹화 시작 시각(Unix epoch 기준 밀리초).
     * @param codec 녹화 영상 코덱 이름.
     * @return SQL 실행에 성공하면 true, DB 비활성화 또는 실행 오류 처리 시 false.
     */
    bool saveStarted(
        std::int64_t profileId, const std::string& filePath,
        std::int64_t startMs, const std::string& codec) const;

    /**
     * @brief 파일 경로로 기존 세그먼트를 찾아 녹화 종료 메타데이터를 갱신한다.
     * @param filePath 갱신할 녹화 파일 경로.
     * @param endMs 녹화 종료 시각(Unix epoch 기준 밀리초).
     * @param fileSize 녹화 파일 크기(바이트).
     * @param codec 녹화 영상 코덱 이름.
     * @param status 저장할 녹화 종료 상태(예: completed, interrupted).
     * @return SQL 실행에 성공하면 true, DB 비활성화 또는 실행 오류 처리 시 false.
     */
    bool saveClosed(
        const std::string& filePath, std::int64_t endMs,
        std::int64_t fileSize, const std::string& codec,
        const std::string& status) const;

    /**
     * @brief recording 상태로 남아 있는 모든 세그먼트를 interrupted 상태로 갱신한다.
     *
     * 서버 시작 시 미완료 녹화 정리에 사용한다. 종료 시각이 없으면 DB 현재 시각을
     * 기록하고, 모든 대상의 has_gap을 true로 설정한다.
     * @return SQL 실행에 성공하면 true, DB 비활성화 또는 실행 오류 처리 시 false.
     */
    bool interruptStaleRecordings() const;
};
