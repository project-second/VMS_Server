#include "IntervalTimer.h"

/* public */

IntervalTimer::IntervalTimer(std::chrono::milliseconds interval,
    Task task, bool run_immediately,
    SchedulePolicy policy) 
        : interval_(interval), task_(std::move(task)),
        run_immediately_(run_immediately),
        policy_(policy), running_(true) {
    worker_ = std::thread([this] { run(); });
}

IntervalTimer::~IntervalTimer() { stop(); }

void IntervalTimer::stop() {
    bool expected = true;
    if (running_.compare_exchange_strong(expected, false)) {
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
}


/* private */

void IntervalTimer::run() {
    using clock = std::chrono::steady_clock;

    // 즉시 1회 실행 옵션
    if (run_immediately_) { safeCallTask(); }

    // 다음 목표 시각 설정
    auto next = clock::now() + interval_;

    std::unique_lock<std::mutex> lk(m_);
    while (running_) {
        // 다음 tick까지 기다리되, stop()이면 즉시 깨어남
        cv_.wait_until(lk, next, [this] { return !running_; });
        if (!running_) break;

        // 실행 중에는 락을 잡지 않기 (stop은 atomic으로 체크)
        lk.unlock();
        safeCallTask();
        lk.lock();

        // 스케줄링 정책에 따라 다음 tick 계산
        const auto now = clock::now();
        switch (policy_) {
            case SchedulePolicy::FixedRate:
                // 원래 예정된 tick 기준으로 누적 (드리프트 최소화)
                next += interval_;
                break;

            case SchedulePolicy::FixedDelay:
                // 실행이 끝난 시점 기준으로 interval (실제 간격 일정)
                next = now + interval_;
                break;

            case SchedulePolicy::SkipMissed:
                // 이미 여러 번 놓쳤다면, 현재 기준으로 재정렬
                if (now > next) { next = now + interval_; } 
                else { next += interval_; }
                break;
        }
    }
}

void IntervalTimer::safeCallTask() noexcept {
    try {
        if (task_) task_();
    } catch (const std::exception& e) {
        // 타이머 스레드가 예외로 죽지 않도록 방어
        std::cerr << "[IntervalTimer] task exception: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "[IntervalTimer] task unknown exception\n";
    }
}