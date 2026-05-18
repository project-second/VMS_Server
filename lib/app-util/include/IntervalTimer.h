#ifndef __INTERVALTIMER_H__
#define __INTERVALTIMER_H__

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

enum class SchedulePolicy {
    /// @brief 매번 "이전 실행 기준"으로 interval을 더함 (기본). 드리프트 최소화.
    FixedRate, 
    
    /// @brief 실행이 늦었으면 늦은 만큼 다음도 밀림 (실제 실행 간격이 일정하게 유지됨)
    FixedDelay, 
    
    /// @brief 많이 늦었으면(여러 주기를 놓쳤으면) 다음 tick을 현재 기준으로 재정렬 (스킵)
    SkipMissed
};

using Task = std::function<void()>;

/// 인터벌 타이머 객체 (정해진 간격으로 등록된 함수를 반복 실행)
class IntervalTimer {
private:
    std::chrono::milliseconds interval_;    /// 인터벌 타이머
    Task task_;                             /// 수행 콜백
    bool run_immediately_;                  /// 즉시 실행 여부
    SchedulePolicy policy_;                 /// 스케줄 정책

    std::atomic<bool> running_;
    std::thread worker_;
    std::mutex m_;
    std::condition_variable cv_;
    
public:
    /**
     * @brief Construct a new Interval Timer object
     * 
     * @param interval          인터벌 타이머 설정 ms
     * @param task              작업 콜백 함수
     * @param run_immediately   즉시 실행 여부 (기본: true)
     * @param policy            스케줄 정책 (기본: SchedulePolicy::FixedRate) 
     */
    IntervalTimer(std::chrono::milliseconds interval,
                  Task task,
                  bool run_immediately = true,
                  SchedulePolicy policy = SchedulePolicy::FixedRate);
    ~IntervalTimer();


    /// @brief 스케줄 중지
    void stop();

private:
    void run();
    void safeCallTask() noexcept;
};

#endif // __INTERVALTIMER_H__