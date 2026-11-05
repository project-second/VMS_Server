#pragma once

#include "HlsVodSession.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct HlsVodConvertResult {
    bool ok = false;
    // 원본 전체 디코딩은 성공했지만 요청 구간에 영상이 없는 경우만 true다.
    bool no_video_in_range = false;
    std::string error_message;

    std::size_t sequence_no = 0;
    std::filesystem::path source_file_path;
    std::filesystem::path output_file_path;

    std::int64_t source_offset_ms = 0;
    std::int64_t requested_duration_ms = 0;
    // 출력된 H.264 프레임의 표시 시각과 길이로 측정한 TS 재생 길이.
    std::int64_t output_duration_ms = 0;
    std::string codec_configuration;

    // 37강부터 source_offset_ms와 requested_duration_ms를
    // GStreamer seek 구간으로 적용한다.
    bool trim_applied = false;
    bool key_unit_seek = true;

    std::int64_t requested_stop_ms = 0;
};

struct HlsVodConvertAllResult {
    bool ok = false;
    std::string error_message;

    std::string session_id;
    std::string camera_id;
    std::filesystem::path cache_dir;
    std::filesystem::path playlist_path;

    std::size_t requested_count = 0;
    std::size_t success_count = 0;
    // 전체 디코딩으로 확인된 빈 시간 범위. 실제 변환 실패 수에는 포함하지 않는다.
    std::size_t skipped_count = 0;
    std::size_t failure_count = 0;

    std::vector<HlsVodConvertResult> segment_results;
};

// MP4 녹화 segment를 HLS용 MPEG-TS segment로 변환하는 클래스다.
//
// 37강 범위:
// - HlsVodSession의 모든 segment plan을 순차 변환
// - source_offset_ms부터 requested_duration_ms만큼 TS로 변환
// - 중간 시작 구간은 원본 처음부터 decode 후 raw frame을 절단하고 re-encode
// - 조각 시작점부터 변환하는 나머지 구간은 기존처럼 빠르게 remux
// - 세션 변환은 출력 시각·TS continuity counter를 조각 사이에 이어 붙인다.
//
// 주의:
// - 정확 절단 경로는 avdec_h264, videoconvert, x264enc가 필요하다.
// - H264 이외 codec은 아직 처리하지 않는다.
class HlsVodConverter {
public:
    HlsVodConvertResult convert_first_segment(
        const HlsVodSession& session
    ) const;

    // no_video_in_range인 항목은 반환 후 재생목록과 세션의 제공 파일 목록에서 제외한다.
    HlsVodConvertAllResult convert_all_segments(
        const HlsVodSession& session
    ) const;

    HlsVodConvertResult convert_segment_to_ts(
        const HlsVodSegment& segment
    ) const;

    static void print_convert_result(
        const HlsVodConvertResult& result
    );

    static void print_convert_all_result(
        const HlsVodConvertAllResult& result
    );

private:
    HlsVodConvertResult convert_segment_to_ts(
        const HlsVodSegment& segment, std::int64_t outputOffsetMs
    ) const;

    HlsVodConvertResult validate_segment(
        const HlsVodSegment& segment
    ) const;
};
