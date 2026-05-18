/** public **/

template <class F, class... Args>
auto ThreadPool::submit(int priority, F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> {
    using R = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<R()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<R> fut = task->get_future();

    {
        std::unique_lock<std::mutex> lock(mutex_);

        not_full_cv_.wait(lock, [this] {
            return stop_ || tasks_.size() < max_queue_size_;
        });

        if (stop_) {
            throw std::runtime_error("ThreadPool stopped");
        }

        tasks_.emplace(priority, [task]() { (*task)(); });
    }

    not_empty_cv_.notify_one();
    return fut;
}

template <typename F, typename... Args>
auto ThreadPool::submit_with_timeout(int priority, std::chrono::milliseconds timeout,
    F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
    using R = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<R()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<R> fut = task->get_future();

    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (!not_full_cv_.wait_for(lock, timeout, [this] {
            return stop_ || tasks_.size() < max_queue_size_;
        })) {
            throw std::runtime_error("submit timeout");
        }

        if (stop_) {
            throw std::runtime_error("ThreadPool stopped");
        }

        tasks_.emplace(priority, [task]() { (*task)(); });
    }

    not_empty_cv_.notify_one();
    return fut;
}