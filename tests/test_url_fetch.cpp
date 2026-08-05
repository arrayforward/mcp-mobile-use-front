/**
 * @file test_url_fetch.cpp
 * @brief URL 公钥拉取功能的 mock 端到端测试
 *
 * 功能：
 *   用一个内置的极简 mock HTTP 服务（裸 socket 监听 127.0.0.1 临时端口，
 *   固定返回 JWKS 响应），覆盖 "--auth-jwt-public-key http://..." 的完整链路：
 *   1. net::fetchUrl 拉取 mock 的 /jwks，校验 HTTP 200 与内容；
 *   2. 设置 MCP_JWT_PUBLIC_KEY 为 mock URL，AuthChecker::fromEnv() 应完成
 *      URL 拉取 -> JWKS 解析 -> RS256 配置，构造带 Bearer token 的请求验证
 *      check() 放行/拒绝行为。
 *
 * 开发思路：
 *   不依赖外部进程（node/python），在测试进程内起一个一次性 mock 服务线程，
 *   保证单测自包含、可在任意 Linux 主机直接运行；Windows 主机因 net 模块
 *   依赖 POSIX 头，本测试仅在定义 MCP_WITH_MOCK_HTTP_TEST 时编译 mock 主体
 *   （CMake 在 NOT WIN32 时自动定义；Windows 手动验证可用 posix shim）。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "test_framework.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#ifdef MCP_WITH_MOCK_HTTP_TEST
#include "mcp/auth.hpp"
#include "net/https_client.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(_WIN32)
// MinGW CRT 无 setenv/unsetenv（仅 shim 手动验证路径需要），用 _putenv 模拟；
// 注意 _putenv 不拷贝字符串，必须让缓冲区常驻（static）
static int setenv(const char* n, const char* v, int) {
    static std::string storage;
    storage = std::string(n) + "=" + v;
    return _putenv(storage.data());
}
static int unsetenv(const char* n) {
    static std::string storage;
    storage = std::string(n) + "=";
    return _putenv(storage.data());
}
#endif
#endif  // MCP_WITH_MOCK_HTTP_TEST

namespace {

/** @brief 读取测试向量文件（RSA_VECTORS_DIR 环境变量可覆盖目录） */
std::string readFixture(const char* name) {
    const char* base = std::getenv("RSA_VECTORS_DIR");
    std::string dir = base ? base : "C:/Users/hubinix/AppData/Local/Temp/opencode/rsa-vectors";
    std::ifstream in(dir + "/" + name, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

#ifdef MCP_WITH_MOCK_HTTP_TEST

/**
 * @struct MockHttpServer
 * @brief 一次性 mock HTTP 服务：监听临时端口，对所有请求固定返回同一份 body
 *
 * 实现思路（伪代码）：
 *   start(body):
 *     socket -> bind(127.0.0.1:0) -> getsockname 取实际端口 -> listen
 *     线程循环: accept -> 读请求(粗读到头部结束即可) -> 写 200 响应 -> close
 *   stop(): 关闭监听 fd 使 accept 退出 -> join 线程
 */
struct MockHttpServer {
    int listenFd = -1;
    int port = 0;
    std::atomic<bool> stopFlag{false};
    std::thread worker;

    /**
     * @brief 启动 mock 服务
     * @param body 对所有请求固定返回的响应体
     * @return 成功返回 true，port 字段为实际监听端口
     */
    bool start(const std::string& body) {
#if defined(_WIN32)
        // shim 手动验证路径：Winsock 必须先初始化（Linux 下为空操作）
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        listenFd = socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd < 0) return false;
        int one = 1;
        setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;  // 临时端口，避免端口冲突
        if (bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
        socklen_t len = sizeof(addr);
        if (getsockname(listenFd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) return false;
        port = ntohs(addr.sin_port);
        if (listen(listenFd, 8) != 0) return false;

        worker = std::thread([this, body]() {
            while (!stopFlag.load()) {
                int c = accept(listenFd, nullptr, nullptr);
                if (c < 0) break;  // listenFd 被 stop() 关闭
                // 粗读请求：mock 场景只需消费掉请求字节即可
                char buf[2048];
                recv(c, buf, sizeof(buf), 0);
                std::string resp = "HTTP/1.1 200 OK\r\ncontent-type: application/json\r\n"
                                   "content-length: " + std::to_string(body.size()) +
                                   "\r\nconnection: close\r\n\r\n" + body;
                send(c, resp.data(), resp.size(), 0);
                close(c);
            }
        });
        return true;
    }

    /** @brief 停止服务并回收线程 */
    void stop() {
        stopFlag.store(true);
        if (listenFd >= 0) close(listenFd);  // 使 accept 立即返回
        if (worker.joinable()) worker.join();
    }
};

/**
 * @brief URL 公钥拉取 mock 测试
 *
 * 伪代码：
 *   读 jwks/good/expired 测试向量
 *   起 mock 服务返回 jwks
 *   fetchUrl(mock /jwks) -> 200 且含 "keys"
 *   setenv MCP_JWT_PUBLIC_KEY=mockUrl -> AuthChecker::fromEnv()
 *   -> enabled() 且 mode==Jwt
 *   -> Bearer good 请求 check()==true；expired/无头 check()==false
 *   unsetenv，stop mock
 */
void testUrlFetchMock() {
    std::string jwks = readFixture("jwks.json");
    std::string good = readFixture("good.jwt");
    std::string expired = readFixture("expired.jwt");
    CHECK(!jwks.empty() && !good.empty() && !expired.empty());

    MockHttpServer mock;
    CHECK(mock.start(jwks));
    std::string url = "http://127.0.0.1:" + std::to_string(mock.port) + "/jwks";

    // 1. fetchUrl 直连 mock：校验 HTTP 拉取本身
    net::HttpResponse resp = net::fetchUrl(url, 5000);
    CHECK(resp.status == 200);
    CHECK(resp.body.find("\"keys\"") != std::string::npos);

    // 2. 完整链路：env(URL) -> fromEnv -> 拉取 JWKS -> RS256 验签
    setenv("MCP_JWT_PUBLIC_KEY", url.c_str(), 1);
    mcp::AuthChecker auth = mcp::AuthChecker::fromEnv();
    CHECK(auth.enabled());
    CHECK(auth.mode() == mcp::AuthChecker::Mode::Jwt);

    net::Request req;
    req.method = "POST";
    req.path = "/mcp";
    req.headers.emplace_back("Authorization", "Bearer " + good);
    CHECK(auth.check(req));  // 有效 JWT 放行

    net::Request reqExpired = req;
    reqExpired.headers[0].second = "Bearer " + expired;
    CHECK(!auth.check(reqExpired));  // 过期 JWT 拒绝

    net::Request reqNoAuth = req;
    reqNoAuth.headers.clear();
    CHECK(!auth.check(reqNoAuth));  // 无 Authorization 拒绝

    unsetenv("MCP_JWT_PUBLIC_KEY");
    mock.stop();
}

#endif  // MCP_WITH_MOCK_HTTP_TEST

}  // namespace

void runUrlFetchTests() {
#ifdef MCP_WITH_MOCK_HTTP_TEST
    testUrlFetchMock();
#endif
}
