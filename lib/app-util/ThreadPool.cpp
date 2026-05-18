#include "ThreadPool.hpp"

/** public **/

ThreadPool::ThreadPool(size_t thread_count, size_t max_queue_size) 
    : stop_(false), immediate_stop_(false), max_queue_size_(max_queue_size) {
    if (thread_count == 0) thread_count = 1;

    for (size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() { shutdown(ShutdownMode::Graceful); }

void ThreadPool::shutdown(ShutdownMode mode) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) return;

        stop_ = true;
        if (mode == ShutdownMode::Immediate) {
            immediate_stop_ = true;
            while (!tasks_.empty()) tasks_.pop();
        }
    }

    not_empty_cv_.notify_all();
    not_full_cv_.notify_all();

    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
}

size_t ThreadPool::queue_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}


/** private **/

void ThreadPool::worker_loop() {
    while (true) {
        Task task(0, nullptr);

        {
            std::unique_lock<std::mutex> lock(mutex_);

            not_empty_cv_.wait(lock, [this] {
                return stop_ || !tasks_.empty();
            });

            if (immediate_stop_) return;
            if (stop_ && tasks_.empty()) return;

            task = std::move(tasks_.top());
            tasks_.pop();

            not_full_cv_.notify_one();
        }

        task.func();
    }
}