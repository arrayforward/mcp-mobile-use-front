#include "http_transport.hpp"

#include <chrono>
#include <random>

namespace mcp {

namespace {

std::string randomSessionId() {
    static std::mt19937_64 rng(std::random_device{}());
    static const char* kHex = "0123456789abcdef";
    std::string id;
    for (int i = 0; i < 32; ++i) id += kHex[rng() & 0xF];
    return id;
}

}  // namespace

void McpHttpTransport::registerRoutes(net::HttpServer& server) {
    server.addRoute("GET", "/sse",
                    [this](const net::Request& req) { return handleSseConnect(req); });
    server.addRoute("POST", "/message",
                    [this](const net::Request& req) { return handleSseMessage(req); });
    server.addRoute("POST", "/mcp",
                    [this](const net::Request& req) { return handleStreamable(req); });
}

std::string McpHttpTransport::createSession() {
    std::string id = randomSessionId();
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    sessions_[id] = std::make_shared<SseSession>();
    return id;
}

std::shared_ptr<McpHttpTransport::SseSession> McpHttpTransport::findSession(
    const std::string& id) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : it->second;
}

void McpHttpTransport::removeSession(const std::string& id) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    sessions_.erase(id);
}

net::Response McpHttpTransport::handleSseConnect(const net::Request& req) {
    if (!auth_.check(req)) return net::Response::json(401, "{\"error\":\"unauthorized\"}");
    std::string sessionId = createSession();

    net::Response resp;
    resp.sse = true;
    resp.sseHandler = [this, sessionId](net::Io& io) {
        auto session = findSession(sessionId);
        if (!session) return;

        if (!io.writeAll("event: endpoint\ndata: /message?sessionId=" + sessionId + "\n\n")) {
            removeSession(sessionId);
            return;
        }

        while (true) {
            std::deque<std::string> pending;
            {
                std::unique_lock<std::mutex> lock(session->m);
                bool hasData = session->cv.wait_for(
                    lock, std::chrono::seconds(15),
                    [&] { return session->closed || !session->queue.empty(); });
                if (session->closed) break;
                if (hasData) pending.swap(session->queue);
            }

            if (pending.empty()) {
                if (!io.writeAll(": keepalive\n\n")) break;
                continue;
            }
            while (!pending.empty()) {
                const std::string& msg = pending.front();
                if (!io.writeAll("event: message\ndata: " + msg + "\n\n")) {
                    session->closed = true;
                    break;
                }
                pending.pop_front();
            }
            if (session->closed) break;
        }
        removeSession(sessionId);
    };
    return resp;
}

net::Response McpHttpTransport::handleSseMessage(const net::Request& req) {
    if (!auth_.check(req)) return net::Response::json(401, "{\"error\":\"unauthorized\"}");
    std::string sessionId = req.queryParam("sessionId");
    auto session = findSession(sessionId);
    if (!session) {
        return net::Response::json(404, "{\"error\":\"unknown sessionId\"}");
    }

    std::string responseJson;
    bool hasResponse = protocol_.handleMessage(req.body, responseJson);
    if (hasResponse && !responseJson.empty()) {
        {
            std::lock_guard<std::mutex> lock(session->m);
            session->queue.push_back(responseJson);
        }
        session->cv.notify_all();
    }
    return net::Response::text(202, "Accepted");
}

net::Response McpHttpTransport::handleStreamable(const net::Request& req) {
    if (!auth_.check(req)) return net::Response::json(401, "{\"error\":\"unauthorized\"}");
    std::string responseJson;
    bool hasResponse = protocol_.handleMessage(req.body, responseJson);
    if (!hasResponse || responseJson.empty()) {
        net::Response resp = net::Response::text(202, "");
        return resp;
    }
    net::Response resp = net::Response::json(200, responseJson);
    return resp;
}

}  // namespace mcp
