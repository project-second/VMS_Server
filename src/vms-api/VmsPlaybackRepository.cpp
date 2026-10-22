/**
 * @file VmsPlaybackRepository.cpp
 * @brief 재생 SQL 매퍼 호출과 조회 행의 녹화 세그먼트 레코드 변환 구현.
 */
#include "VmsPlaybackRepository.h"

#include "DbRowHelpers.h"
#include "DbRuntime.hpp"

namespace {

/**
 * @brief 타입을 보존한 DB 조회 행을 녹화 세그먼트 레코드로 변환한다.
 *
 * seg_id, profile_id, file_path, start_ms는 필수로 읽는다.
 * 나머지 컬럼은 조회 결과에 포함된 경우 읽고, 없는 컬럼은 구조체 기본값을 유지한다.
 * @param row playback 매퍼가 반환한 조회 행.
 * @return 변환된 레코드. 필수 컬럼을 읽거나 변환할 수 없으면 std::nullopt.
 */
std::optional<PlaybackSegmentRecord> toSegment(const DbTemplate::TypedRow& row) {
    PlaybackSegmentRecord value;
    if (!DbRow::getInt64Cell(row, "seg_id", value.segmentId) ||
        !DbRow::getInt64Cell(row, "profile_id", value.profileId) ||
        !DbRow::getStringCell(row, "file_path", value.filePath) ||
        !DbRow::getInt64Cell(row, "start_ms", value.startMs)) {
        return std::nullopt;
    }
    // 종료 시각은 실제 값 변환에 성공했을 때만 optional에 저장한다.
    std::int64_t endMs = 0;
    if (DbRow::getInt64Cell(row, "end_ms", endMs)) value.endMs = endMs;
    DbRow::getInt64Cell(row, "file_size", value.fileSize);
    DbRow::getStringCell(row, "codec", value.codec);
    DbRow::getStringCell(row, "status", value.status);
    DbRow::getBoolCell(row, "has_gap", value.hasGap);
    // 장치·채널 정보는 단건 조회의 조인 결과에 포함되며 페이지·구간 조회에서는 기본값을 유지한다.
    DbRow::getInt64Cell(row, "device_id", value.deviceId);
    DbRow::getInt64Cell(row, "channel_id", value.channelId);
    DbRow::getStringCell(row, "device_name", value.deviceName);
    DbRow::getStringCell(row, "channel_name", value.channelName);
    return value;
}

} // namespace

// 활성 프로파일 선택은 매퍼에 맡기고, 조회된 첫 행에서 재생 대상 정보를 구성한다.
std::optional<PlaybackTargetRecord> VmsPlaybackRepository::findTarget(
    std::int64_t deviceId, std::int64_t channelId) const {
    DbTemplate::ParamMap params{{"device_id", deviceId}, {"channel_id", channelId}};
    const auto rows = AppDb::select("playback.findTarget", params);
    if (rows.empty()) return std::nullopt;
    PlaybackTargetRecord target;
    if (!DbRow::getInt64Cell(rows.front(), "device_id", target.deviceId) ||
        !DbRow::getInt64Cell(rows.front(), "channel_id", target.channelId) ||
        !DbRow::getInt64Cell(rows.front(), "profile_id", target.profileId)) return std::nullopt;
    // 표시 이름은 선택 항목으로 읽으며, 대상 반환에는 세 식별자만 필수로 요구한다.
    DbRow::getStringCell(rows.front(), "device_name", target.deviceName);
    DbRow::getStringCell(rows.front(), "channel_name", target.channelName);
    return target;
}

// 매퍼의 페이지 결과에서 전체 건수를 읽고 변환에 성공한 세그먼트만 목록에 추가한다.
PlaybackSegmentPage VmsPlaybackRepository::findPage(
    std::int64_t profileId, std::int64_t limit, std::int64_t offset) const {
    DbTemplate::ParamMap params{{"profile_id", profileId}, {"limit", limit}, {"offset", offset}};
    PlaybackSegmentPage page;
    for (const auto& row : AppDb::select("playback.findPage", params)) {
        std::int64_t count = 0;
        if (DbRow::getInt64Cell(row, "total_count", count)) page.totalCount = count;
        if (auto item = toSegment(row)) page.items.push_back(std::move(*item));
    }
    // 변환된 페이지 항목이 없으면 전체 건수를 별도로 조회해 범위 밖의 페이지에도 총계를 제공한다.
    if (page.items.empty()) {
        const auto rows = AppDb::select("playback.countByProfile", {{"profile_id", profileId}});
        if (!rows.empty()) DbRow::getInt64Cell(rows.front(), "total_count", page.totalCount);
    }
    return page;
}

// 시간 구간 필터링과 정렬은 매퍼가 수행하며, 그 순서를 유지해 변환 가능한 행만 반환한다.
std::vector<PlaybackSegmentRecord> VmsPlaybackRepository::findRange(
    std::int64_t profileId, std::int64_t fromMs, std::int64_t toMs) const {
    DbTemplate::ParamMap params{{"profile_id", profileId}, {"from_ms", fromMs}, {"to_ms", toMs}};
    std::vector<PlaybackSegmentRecord> result;
    for (const auto& row : AppDb::select("playback.findRange", params)) {
        if (auto item = toSegment(row)) result.push_back(std::move(*item));
    }
    return result;
}

// 상태 제한 없이 단건을 조회하고 공통 변환 함수로 장치·채널 부가 정보까지 읽는다.
std::optional<PlaybackSegmentRecord> VmsPlaybackRepository::findById(
    std::int64_t segmentId) const {
    const auto rows = AppDb::select("playback.findById", {{"seg_id", segmentId}});
    if (rows.empty()) return std::nullopt;
    return toSegment(rows.front());
}

// 파일 경로가 이미 존재하면 녹화 시작 상태로 초기화하는 등록·갱신 매퍼를 실행한다.
bool VmsPlaybackRepository::saveStarted(
    std::int64_t profileId, const std::string& filePath,
    std::int64_t startMs, const std::string& codec) const {
    return AppDb::insert("playback.saveStarted", {
        {"profile_id", profileId}, {"file_path", filePath},
        {"start_ms", startMs}, {"codec", codec}});
}

// 파일 경로에 대응하는 기존 행의 종료 메타데이터를 UPDATE하고 SQL 실행 성공 여부를 반환한다.
bool VmsPlaybackRepository::saveClosed(
    const std::string& filePath, std::int64_t endMs,
    std::int64_t fileSize, const std::string& codec,
    const std::string& status) const {
    return AppDb::update("playback.saveClosed", {
        {"file_path", filePath}, {"end_ms", endMs}, {"file_size", fileSize},
        {"codec", codec}, {"status", status}});
}

// recording 상태의 모든 행을 중단 처리하고 종료 시각 보정과 공백 표시를 매퍼에서 수행한다.
bool VmsPlaybackRepository::interruptStaleRecordings() const {
    return AppDb::update("playback.interruptStale", {});
}
