/**
 * @file protocol.hpp
 * @brief MCP 协议层（mcp::Protocol）——JSON-RPC 2.0 消息封装与分发
 *
 * 功能：
 *   实现 Model Context Protocol（协议版本 2024-11-05）的核心消息处理：
 *   initialize / ping / tools/list / tools/call 等方法的路由分发，
 *   以及 JSON-RPC 2.0 规范的结果（result）与错误（error）响应封装。
 *
 * 开发思路：
 *   1. 与传输层完全解耦：Protocol 只接收/返回 JSON 字符串，不关心
 *      stdio 还是 HTTP/SSE，便于两种传输（StdioServer / McpHttpTransport）复用。
 *   2. 工具列表与调用委托给 tool::allTools() 注册表（见 tool/base.hpp），
 *      协议层只做参数校验与异常兜底，保持单一职责。
 *   3. 通知（notification，无 id 或 id 为 null）按 JSON-RPC 规范不返回响应，
 *      handleMessage 返回 false 表示"无需回复"。
 *   4. 错误码遵循 JSON-RPC 2.0：-32700 解析错误、-32600 无效请求、
 *      -32601 方法不存在、-32603 内部错误。
 *
 * @author hubin
 * @date 2026-08-05
 */
#pragma once

#include <string>

#include "../json/json.hpp"

namespace mcp {

/**
 * @class Protocol
 * @brief MCP 协议处理器：解析 JSON-RPC 请求并分发到对应方法
 *
 * 开发思路：
 *   无状态设计（仅构造/析构），所有状态保存在 tool 注册表与传输层中，
 *   因此单个实例可被多路传输并发调用；handleMessage 为唯一对外入口，
 *   内部按 method 字符串分发到 onXxx 私有方法，统一经 makeResult/makeError
 *   封装响应，保证输出格式一致。
 *
 * @author hubin
 * @date 2026-08-05
 */
class Protocol {
public:
    Protocol();

    /**
     * @brief 处理一条 JSON-RPC 请求文本
     * @param requestJson  请求 JSON 字符串（一行）
     * @param responseJson [out] 响应 JSON 字符串（无需响应时保持为空）
     * @return true 表示产生了响应需回发；false 表示通知类消息，无需回复
     *
     * 实现思路（伪代码）：
     *   parse 请求 -> 失败回 -32700
     *   校验对象与 method 字段 -> 失败回 -32600
     *   若是通知（notifications/ 前缀或无 id）-> return false
     *   dispatch 分发，异常兜底 -32603 -> dump 输出
     */
    bool handleMessage(const std::string& requestJson, std::string& responseJson);

    /** @brief 返回 MCP 协议版本号（如 "2024-11-05"） */
    static const char* protocolVersion();
    /** @brief 返回服务名（"mcp_mobile_use"） */
    static const char* serverName();
    /** @brief 返回服务版本号 */
    static const char* serverVersion();

private:
    /**
     * @brief 按 method 分发到具体处理函数
     * @param id     请求 id（原样回显）
     * @param method 方法名
     * @param params 方法参数对象
     * @return 完整 JSON-RPC 响应值（result 或 error）
     */
    mj::Value dispatch(const mj::Value& id, const std::string& method,
                       const mj::Value& params);
    /** @brief 构造 initialize 响应：协议版本 + capabilities + serverInfo */
    mj::Value onInitialize();
    /** @brief 构造 tools/list 响应：遍历工具注册表输出 name/description/inputSchema */
    mj::Value onToolsList();
    /** @brief 处理 tools/call：按 name 查找工具并调用其 handler，异常转错误结果 */
    mj::Value onToolsCall(const mj::Value& params);

    /** @brief 封装成功响应 {jsonrpc,id,result} */
    mj::Value makeResult(const mj::Value& id, const mj::Value& result);
    /** @brief 封装错误响应 {jsonrpc,id,error:{code,message}} */
    mj::Value makeError(const mj::Value& id, int code, const std::string& message);
};

}  // namespace mcp
