#include "HttpClientPool.h"

#include "LogSync.h"
#include <utility>

HttpClientPool::HttpClientPool(int thread_cnt, int max_que) 
    : pool(thread_cnt, max_que) {}

HttpClientPool::~HttpClientPool() {
    shutdown(ThreadPool::ShutdownMode::Graceful);
}

void HttpClientPool::setClientEvent(std::string eventName
    , RequestTask requestTask) {
    std::lock_guard<std::mutex> lock(clientEventsMutex);
    clientEvents.insert_or_assign(std::move(eventName), std::move(requestTask));
}

void HttpClientPool::removeClientEvent(const std::string& eventName) {
    std::lock_guard<std::mutex> lock(clientEventsMutex);
    clientEvents.erase(eventName);
}


void HttpClientPool::shutdown(ThreadPool::ShutdownMode mode) {
    pool.shutdown(mode);
}
bool HttpClientPool::eventSubmit(int priority, std::string eventName, std::string ipaddr
        , int port, Args args) {
    RequestTask task;
    {
        std::lock_guard<std::mutex> lock(clientEventsMutex);
        auto it = clientEvents.find(eventName);
        if (it == clientEvents.end()) {
            LogSync::error("[HttpClientPool] unknown event: ", eventName);
            return false;
        }
        task = it->second;
    }

    try {
        pool.submit(priority, std::move(task), std::move(ipaddr), port, std::move(args));
        return true;
    } catch (const std::exception& e) {
        LogSync::error("[HttpClientPool] submit failed for event ", eventName, ": ", e.what());
        return false;
    }
}

bool HttpClientPool::loadDigestSession(const std::string& session_key, HttpDigestSession& out) {
    std::lock_guard<std::mutex> lock(digestSessionsMutex);
    auto it = digestSessions.find(session_key);
    if (it == digestSessions.end()) {
        return false;
    }
    out = it->second;
    return true;
}

void HttpClientPool::saveDigestSession(const std::string& session_key, const HttpDigestSession& session) {
    std::lock_guard<std::mutex> lock(digestSessionsMutex);
    digestSessions[session_key] = session;
}

void HttpClientPool::clearDigestSession(const std::string& session_key) {
    std::lock_guard<std::mutex> lock(digestSessionsMutex);
    digestSessions.erase(session_key);
}
