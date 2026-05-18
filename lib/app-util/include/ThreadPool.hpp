#ifndef __THREADPOOL_H__
#define __THREADPOOL_H__

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include <stdexcept>
#include <atomic>
#include <chrono>

class ThreadPool {
public:
    enum class ShutdownMode {
        Graceful,   // 큐에 남은 작업 처리 후 종료
        Immediate   // 남은 작업 무시하고 즉시 종료
    };

private:
    struct Task {
        int priority;
        std::function<void()> func;

        Task(int p, std::function<void()> f)
            : priority(p), func(std::move(f)) {}

        bool operator<(const Task& other) const {
            return priority < other.priority; // 큰 값이 먼저 실행
        }
    };

    std::vector<std::thread> workers_;
    std::priority_queue<Task> tasks_;

    mutable std::mutex mutex_;
    std::condition_variable not_empty_cv_;
    std::condition_variable not_full_cv_;

    std::atomic<bool> stop_;
    std::atomic<bool> immediate_stop_;

    size_t max_queue_size_;

public:
    explicit ThreadPool(
        size_t thread_count = std::thread::hardware_concurrency(),
        size_t max_queue_size = 1024
    );

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    ~ThreadPool();

    /**
     * @brief 작업을 스레드풀에 제출합니다.
     *
     * 전달된 callable 객체와 인자들을 하나의 작업으로 래핑하여
     * 내부 작업 큐에 enqueue 합니다.
     *
     * 작업은 가용한 worker 스레드 중 하나에서 비동기적으로 실행되며,
     * 반환값은 std::future를 통해 획득할 수 있습니다.
     *
     * 이 함수는 thread-safe 합니다.
     *
     * @tparam F      호출 가능한 타입 (함수, 람다, functor 등)
     * @tparam Args   F에 전달될 인자 타입들
     *
     * @param priority  작업 우선순위 (큰 값이 먼저 실행됨)
     * @param f         실행할 callable 객체
     * @param args      callable에 전달될 인자들
     *
     * @return std::future<std::invoke_result_t<F, Args...>>
     *         작업의 실행 결과를 비동기적으로 받을 수 있는 future 객체
     *
     * @throws std::runtime_error
     *         스레드풀이 이미 shutdown 상태인 경우 발생
     *
     * @note 작업 실행 중 발생한 예외는 future.get() 호출 시 재전파됩니다.
     */
    template <class F, class... Args>
    auto submit(int priority, F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    /**
     * @brief 우선순위 및 타임아웃을 지정하여 작업을 제출합니다.
     *
     * 지정된 priority 값에 따라 내부 우선순위 큐에 삽입됩니다.
     * 값이 클수록 높은 우선순위를 갖습니다.
     *
     * 작업 큐가 가득 찬 경우, 지정된 timeout 동안 대기합니다.
     * timeout 내에 enqueue가 불가능하면 예외를 발생시킵니다.
     *
     * 이 함수는 thread-safe 합니다.
     *
     * @tparam F      호출 가능한 타입
     * @tparam Args   F에 전달될 인자 타입들
     *
     * @param priority  작업 우선순위 (큰 값이 먼저 실행됨)
     * @param timeout   큐가 가득 찼을 경우 대기할 최대 시간
     * @param f         실행할 callable 객체
     * @param args      callable에 전달할 인자들
     *
     * @return std::future<std::invoke_result_t<F, Args...>>
     *         작업 결과를 비동기적으로 수신할 수 있는 future 객체
     *
     * @throws std::runtime_error
     *         - 스레드풀이 shutdown 상태인 경우
     *         - timeout 내에 작업을 enqueue 하지 못한 경우
     *
     * @note 과부하 상황에서 backpressure를 구현할 때 유용합니다.
     */
    template <typename F, typename... Args>
    auto submit_with_timeout(int priority, std::chrono::milliseconds timeout,
        F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

    /**
     * @brief 스레드풀을 종료합니다.
     *
     * 지정된 종료 모드에 따라 동작합니다:
     *
     * - ShutdownMode::Graceful
     *   - 새로운 작업 제출을 차단합니다.
     *   - 큐에 남아 있는 작업을 모두 처리한 후 worker 스레드를 종료합니다.
     *
     * - ShutdownMode::Immediate
     *   - 새로운 작업 제출을 차단합니다.
     *   - 대기 중인 작업을 모두 폐기합니다.
     *   - 가능한 한 빠르게 worker 스레드를 종료합니다.
     *
     * 이 함수는 thread-safe 합니다.
     *
     * @param mode 종료 동작 모드
     *
     * @note shutdown 이후 submit 계열 함수는 예외를 발생시킵니다.
     * @note 소멸자에서는 Graceful 모드로 자동 호출됩니다.
     */
    void shutdown(ShutdownMode mode);

    size_t queue_size() const;

private:
    void worker_loop();
};

#include "ThreadPool.tpp"

#endif // __THREADPOOL_H__