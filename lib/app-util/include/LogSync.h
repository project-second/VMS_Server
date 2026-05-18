#ifndef __LOGSYNC_H__
#define __LOGSYNC_H__

#include <iostream>
#include <mutex>
#include <utility>

namespace LogSync {

inline std::mutex& log_mutex() {
    static std::mutex m;
    return m;
}

template <typename... Args>
inline void info(Args&&... args) {
    std::lock_guard<std::mutex> lock(log_mutex());
    (std::cout << ... << std::forward<Args>(args)) << '\n';
}

template <typename... Args>
inline void error(Args&&... args) {
    std::lock_guard<std::mutex> lock(log_mutex());
    (std::cerr << ... << std::forward<Args>(args)) << '\n';
}

} // namespace LogSync

#endif // __LOGSYNC_H__
