/**
 * @file http_transport.hpp
 * @brief HTTP 传输层（mcp::McpHttpTransport）——/healthz /sse /message /mcp 路由
 *
 * 功能：
 *   把 MCP 协议挂载到 HTTP 服务上，提供四种端点：
 *   - GET  /healthz：健康检查（免鉴权，供探活/负载均衡）；
 *   - GET  /sse：建立 SSE 长连接，下发 endpoint 事件并持续推送 message 事件；
 *   - POST /message：SSE 会话内的 JSON-RPC 上行通道（202 Accepted，
 *     响应经 SSE 通道异步推送）；
 *   - POST /mcp：Streamable HTTP 模式，请求-响应同步返回（通知返回 202 空体）。
 *
 * 开发思路：
 *   1. 在 transport 入口统一做 auth 检查（/healthz 除外），协议层无感知。
 *   2. SSE 会话以 SseSession 表示：互斥锁 + 条件变量 + 消息队列，
 *      /message 线程入队并 notify，/sse 线程出队写流；15 秒无消息发
 *      keepalive 注释帧防代理断连。
 *   3. 会话表 m_sessions 用独立互斥锁保护，shared_ptr 管理生命周期，
 *      连接断开或写失败时移除会话。
 *
 * @author hubin
 * @date 2026-08-05
 */
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

/**
 * @class McpHttpTransport
 * @brief MCP-over-HTTP 传输：路由注册、鉴权、SSE 会话管理
 *
 * 开发思路：
 *   路由处理函数只做三件事：鉴权 -> 找/建会话 -> 调 Protocol，
 *   协议逻辑全部复用 mcp::Protocol；SSE 推送与上行解耦，
 *   用生产者（/message）-消费者（/sse handler）队列模型实现。
 *
 * @author hubin
 * @date 2026-08-05
 */
class McpHttpTransport {
public:
    /** @brief 设置鉴权检查器（注册路由前调用） */
    void setAuth(const AuthChecker& auth) { m_auth = auth; }
    /**
     * @brief 把四个 MCP 端点注册到 HTTP 服务
     * @param server HTTP 服务器实例
     */
    void registerRoutes(net::HttpServer& server);

private:
    /**
     * @struct SseSession
     * @brief 一条 SSE 长连接的状态：消息队列 + 关闭标志
     *
     * 开发思路：
     *   /message（生产者）加锁入队后 cv.notify_all 唤醒 /sse 消费者线程；
     *   closed 标志用于写失败/对端断开时让两侧线程退出。
     */
    struct SseSession {
        std::mutex m;
        std::condition_variable cv;
        std::deque<std::string> queue;
        bool closed = false;
    };

    /** @brief GET /healthz：返回 {"status":"ok",name,version}（免鉴权） */
    static net::Response handleHealth();
    /** @brief GET /sse：建会话、推 endpoint 事件、循环推 message/keepalive */
    net::Response handleSseConnect(const net::Request& req);
    /** @brief POST /message：处理上行 JSON-RPC，响应入队等待 SSE 推送 */
    net::Response handleSseMessage(const net::Request& req);
    /** @brief POST /mcp：Streamable 模式，同步返回 JSON 响应或 202 */
    net::Response handleStreamable(const net::Request& req);

    /** @brief 创建会话并返回 32 位十六进制随机 id */
    std::string createSession();
    /** @brief 按 id 查会话，不存在返回 nullptr */
    std::shared_ptr<SseSession> findSession(const std::string& id);
    /** @brief 移除会话 */
    void removeSession(const std::string& id);

    Protocol m_protocol;
    AuthChecker m_auth;
    std::mutex m_sessionsMutex;
    std::map<std::string, std::shared_ptr<SseSession>> m_sessions;
};

}  // namespace mcp
