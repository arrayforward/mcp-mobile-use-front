/**
 * @file stdio_server.hpp
 * @brief stdio 传输服务（mcp::StdioServer）——stdin/stdout 行分隔 JSON-RPC
 *
 * 功能：
 *   以标准输入/输出为传输载体的 MCP 服务：每行一条 JSON-RPC 消息，
 *   从 stdin 读请求，把响应写回 stdout。适配 Claude Desktop 等以
 *   子进程管道方式接入 MCP server 的客户端。
 *
 * 开发思路：
 *   极简循环：getline 逐行读 -> Protocol::handleMessage -> 有响应则
 *   输出一行并 flush；协议处理全部委托给 mcp::Protocol，本类只做 IO 适配。
 *
 * @author hubin
 * @date 2026-08-05
 */
#pragma once

#include "protocol.hpp"

namespace mcp {

/**
 * @class StdioServer
 * @brief 基于 stdin/stdout 的行分隔 JSON-RPC 服务
 *
 * 开发思路：
 *   单线程阻塞循环即可满足管道场景（MCP 客户端串行收发），
 *   无需并发与缓冲管理，复杂度全部下沉到 Protocol。
 *
 * @author hubin
 * @date 2026-08-05
 */
class StdioServer {
public:
    /**
     * @brief 运行主循环直到 stdin EOF
     * @return 进程退出码（恒为 0）
     */
    int run();

private:
    Protocol m_protocol;
};

}  // namespace mcp
