#ifndef __HTTPCLIENTPOOL_H__
#define __HTTPCLIENTPOOL_H__

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "ThreadPool.hpp"
#include "HttpClient.h"

class HttpClientPool : public HttpAuthSessionProvider {
public:
    using Arg = std::variant<std::monostate, int, float, double, char, std::string, std::int64_t
        , std::vector<std::variant<std::monostate, int, float, double, char, std::string, std::int64_t>>
        , std::unordered_map<std::string, std::variant<std::monostate, int, float, double, char, std::string, std::int64_t>>>;
    using Args = std::unordered_map<std::string, Arg>;
    using RequestTask = std::function<void(std::string, int, Args)>;

private:
    ThreadPool pool;    // client 요청을 실행할 스레드 풀 모음
    std::mutex clientEventsMutex;
    std::unordered_map<std::string, RequestTask> clientEvents;
    std::mutex digestSessionsMutex;
    std::unordered_map<std::string, HttpDigestSession> digestSessions;

public:

    /**
     * @brief Construct a new Http Client Pool object
     * 
     * @param thread_cnt 쓰레드 수
     * @param max_que 최대 작업 큐
     */
    HttpClientPool(int thread_cnt, int max_que);
    ~HttpClientPool();

    void setClientEvent(std::string eventName, RequestTask requestTask);
    void removeClientEvent(const std::string& eventName);
    void shutdown(ThreadPool::ShutdownMode mode = ThreadPool::ShutdownMode::Graceful);

    bool eventSubmit(int priority, std::string eventName, std::string ipaddr
        , int port, Args args);

    bool loadDigestSession(const std::string& session_key, HttpDigestSession& out) override;
    void saveDigestSession(const std::string& session_key, const HttpDigestSession& session) override;
    void clearDigestSession(const std::string& session_key) override;
};

#endif // __HTTPCLIENTPOOL_H__
