#pragma once

#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "../net/http_server.hpp"
#include "auth.hpp"
#include "protocol.hpp"

namespace mcp {

class McpHttpTransport {
public:
    void setAuth(const AuthChecker& auth) { auth_ = auth; }
    void registerRoutes(net::HttpServer& server);

private:
    struct SseSession {
        std::mutex m;
        std::condition_variable cv;
        std::deque<std::string> queue;
        bool closed = false;
    };

    net::Response handleSseConnect(const net::Request& req);
    net::Response handleSseMessage(const net::Request& req);
    net::Response handleStreamable(const net::Request& req);

    std::string createSession();
    std::shared_ptr<SseSession> findSession(const std::string& id);
    void removeSession(const std::string& id);

    Protocol protocol_;
    AuthChecker auth_;
    std::mutex sessionsMutex_;
    std::map<std::string, std::shared_ptr<SseSession>> sessions_;
};

}  // namespace mcp
