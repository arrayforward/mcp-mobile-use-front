/**
 * @file http_transport.cpp
 * @brief HTTP 传输层实现——路由注册、鉴权入口、SSE 会话推送循环
 *
 * 功能：
 *   实现 http_transport.hpp 声明的 McpHttpTransport：
 *   - 四个端点（/healthz /sse /message /mcp）的处理逻辑；
 *   - 会话 id 生成与 m_sessions 表的增删查；
 *   - SSE 长连接内的消息推送循环（15 秒空闲发 keepalive）。
 *
 * 开发思路：
 *   1. 鉴权在各 handler 第一行统一执行，/healthz 静态处理免鉴权；
 *   2. /sse 返回的 Response 携带 sseHandler 闭包，由 HTTP 服务器在连接
 *      线程中执行，闭包内按 "wait_for 队列 -> 批量写出" 循环；
 *   3. 写流失败立即置 closed 并退出循环，最后统一 removeSession，
 *      保证会话表不泄漏。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "http_transport.hpp"

#include <chrono>
#include <random>

namespace mcp {

namespace {

/**
 * @brief 生成 32 位十六进制随机会话 id
 * @return 32 字符小写十六进制串
 */
std::string randomSessionId() {
    static std::mt19937_64 rng(std::random_device{}());
    static const char* kHex = "0123456789abcdef";
    std::string id;
    for (int i = 0; i < 32; ++i) id += kHex[rng() & 0xF];
    return id;
}

}  // namespace

void McpHttpTransport::registerRoutes(net::HttpServer& server) {
    server.addRoute("GET", "/healthz",
                    [](const net::Request&) { return McpHttpTransport::handleHealth(); });
    server.addRoute("GET", "/sse",
                    [this](const net::Request& req) { return handleSseConnect(req); });
    server.addRoute("POST", "/message",
                    [this](const net::Request& req) { return handleSseMessage(req); });
    server.addRoute("POST", "/mcp",
                    [this](const net::Request& req) { return handleStreamable(req); });
}

net::Response McpHttpTransport::handleHealth() {
    // 健康检查：不校验鉴权，供外部负载均衡/探活使用
    mj::Value info = mj::Value::object();
    info["status"] = "ok";
    info["name"] = Protocol::serverName();
    info["version"] = Protocol::serverVersion();
    return net::Response::json(200, info.dump());
}

std::string McpHttpTransport::createSession() {
    std::string id = randomSessionId();
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    m_sessions[id] = std::make_shared<SseSession>();
    return id;
}

std::shared_ptr<McpHttpTransport::SseSession> McpHttpTransport::findSession(
    const std::string& id) {
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    auto it = m_sessions.find(id);
    return it == m_sessions.end() ? nullptr : it->second;
}

void McpHttpTransport::removeSession(const std::string& id) {
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    m_sessions.erase(id);
}

net::Response McpHttpTransport::handleSseConnect(const net::Request& req) {
    // transport 入口统一鉴权
    if (!m_auth.check(req)) return net::Response::json(401, "{\"error\":\"unauthorized\"}");
    std::string sessionId = createSession();

    net::Response resp;
    resp.sse = true;
    // sseHandler 由 HTTP 服务器在该连接线程中执行，直至连接结束
    resp.sseHandler = [this, sessionId](net::Io& io) {
        auto session = findSession(sessionId);
        if (!session) return;

        // 首帧：告知客户端上行 POST 的端点（MCP SSE 规范）
        if (!io.writeAll("event: endpoint\ndata: /message?sessionId=" + sessionId + "\n\n")) {
            removeSession(sessionId);
            return;
        }

        // 推送循环：等待队列有数据或 15 秒超时 -> 批量写出 / keepalive
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

            // 15 秒无消息：发注释帧保活，防止中间代理断连
            if (pending.empty()) {
                if (!io.writeAll(": keepalive\n\n")) break;
                continue;
            }
            // 逐条写出 message 事件；写失败说明连接已断，置 closed 退出
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
    if (!m_auth.check(req)) return net::Response::json(401, "{\"error\":\"unauthorized\"}");
    std::string sessionId = req.queryParam("sessionId");
    auto session = findSession(sessionId);
    if (!session) {
        return net::Response::json(404, "{\"error\":\"unknown sessionId\"}");
    }

    // 处理 JSON-RPC；有响应则入队并唤醒 /sse 推送线程，本身回 202
    std::string responseJson;
    bool hasResponse = m_protocol.handleMessage(req.body, responseJson);
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
    if (!m_auth.check(req)) return net::Response::json(401, "{\"error\":\"unauthorized\"}");
    // Streamable 模式：同步返回 JSON；通知类消息无响应则回 202 空体
    std::string responseJson;
    bool hasResponse = m_protocol.handleMessage(req.body, responseJson);
    if (!hasResponse || responseJson.empty()) {
        net::Response resp = net::Response::text(202, "");
        return resp;
    }
    net::Response resp = net::Response::json(200, responseJson);
    return resp;
}

}  // namespace mcp
