/**
 * @file stdio_server.cpp
 * @brief stdio 传输实现——逐行读取 JSON-RPC，响应写回 stdout
 *
 * 功能：
 *   实现 StdioServer::run()：从 stdin 按行读请求（兼容 \r\n 行尾），
 *   交给 Protocol 处理后把响应以单行 JSON 写回 stdout 并立即 flush。
 *
 * 开发思路：
 *   1. sync_with_stdio(false) 提升 IO 吞吐；
 *   2. 空行跳过、通知类消息无响应时不输出，保证 stdout 只承载协议消息，
 *      符合 MCP stdio 传输"stdout 只能是 JSON-RPC"的约束（日志须走 stderr）。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "stdio_server.hpp"

#include <iostream>

namespace mcp {

int StdioServer::run() {
    std::ios::sync_with_stdio(false);

    std::string line;
    while (std::getline(std::cin, line)) {
        // 兼容 Windows \r\n 行尾，去掉末尾 \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        // 通知类消息 handleMessage 返回 false 或空响应，不输出
        std::string response;
        if (m_protocol.handleMessage(line, response) && !response.empty()) {
            std::cout << response << "\n";
            std::cout.flush();
        }
    }
    return 0;
}

}  // namespace mcp
