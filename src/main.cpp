/**
 * @file main.cpp
 * @brief mcp_mobile_use 服务进程入口——命令行解析、auth 初始化与传输层启动
 *
 * 功能：
 *   解析命令行参数（传输方式、端口、后端、auth、TLS 等），初始化工具层
 *   全局 provider，然后按传输方式启动 stdio 服务或 HTTP(S) 服务
 *   （sse / streamable-http），并进入信号驱动的退出循环。
 *
 * 开发思路：
 *   1. 手写 argv 顺序扫描解析，避免引入第三方命令行库（零依赖约束）；
 *      未知参数直接报错退出，防止拼写错误被静默忽略。
 *   2. auth 优先级：JWT（--auth-jwt-secret / --auth-jwt-public-key）
 *      > 静态 token（--auth-token）> 环境变量（MCP_AUTH_TOKEN 等）> 无鉴权；
 *      RS256 公钥支持本地文件/内联 PEM/http(s) URL 三种来源（见 loadRs256Key）。
 *   3. HTTP 传输下注册 /sse、/message、/mcp 路由后进入 200ms 轮询循环，
 *      由 SIGINT/SIGTERM 置位 gStop 触发优雅停机。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <string>
#include <thread>

#include "mcp/auth.hpp"
#include "mcp/http_transport.hpp"
#include "mcp/jwt.hpp"
#include "mcp/stdio_server.hpp"
#include "net/http_server.hpp"
#include "net/https_client.hpp"
#include "service/cloud_config.hpp"
#include "service/provider.hpp"
#include "tool/base.hpp"

namespace {

/** @brief 全局停机标志（SIGINT/SIGTERM 信号处理器置位） */
std::atomic<bool> gStop{false};

/**
 * @brief 信号处理器：置位停机标志
 * @param 信号编号（未使用）
 */
void onSignal(int) {
    gStop = true;
}

/**
 * @brief 加载 RS256 公钥到 JWT 验证器
 * @param verifier JWT 验证器（加载成功后即可验签 RS256 token）
 * @param source   公钥来源，支持三种形式：
 *                 1) 本地 PEM/x509 证书文件内容或路径对应的内容；
 *                 2) 内联 PEM 字符串（含 "-----BEGIN" 头）；
 *                 3) http(s) URL——先拉取响应体，再自动识别 PEM 或 JWKS。
 * @return 加载成功返回 true，拉取失败或格式无法识别返回 false
 *
 * 伪代码：
 *   1. 若 source 以 http(s):// 开头 -> fetchUrl 拉取（30s 超时），
 *      失败打印错误并返回 false，成功则取响应体作为 key material；
 *   2. material 含 "-----BEGIN" -> 按 PEM 加载（loadRs256Pem）；
 *   3. 否则按 JWKS JSON 加载（loadRs256Jwks）。
 */
// 加载 RS256 公钥：支持本地 PEM/x509 文件路径、http(s) URL（自动识别 PEM 或 JWKS 响应）
bool loadRs256Key(mcp::JwtVerifier& verifier, const std::string& source) {
    std::string material = source;
    // 来源为 URL：先通过 HTTPS 客户端拉取公钥材料
    if (source.compare(0, 7, "http://") == 0 || source.compare(0, 8, "https://") == 0) {
        net::HttpResponse resp = net::fetchUrl(source, 30000);
        if (!resp.ok()) {
            fprintf(stderr, "failed to fetch public key from %s: %s\n", source.c_str(),
                    resp.error.c_str());
            return false;
        }
        material = resp.body;
    }
    // 内容含 PEM 头标记 -> 按 PEM 公钥/证书解析
    if (material.find("-----BEGIN") != std::string::npos) {
        return verifier.loadRs256Pem(material);
    }
    // 否则按 JWKS（JSON Web Key Set）解析
    return verifier.loadRs256Jwks(material);
}

/**
 * @brief 打印命令行用法
 * @param prog 程序名（argv[0]）
 */
void usage(const char* prog) {
    fprintf(stderr,
            "Usage: %s [options]\n"
            "  -t, --transport <type>   transport type: stdio | sse | streamable-http | http "
            "(default stdio)\n"
            "  -p, --port <port>        listen port for http transports (default 8080)\n"
            "  -b, --backend <backend>  default execution backend: adb | cloud (default adb)\n"
            "      --bind <addr>        bind address for http transports (default 0.0.0.0)\n"
            "      --auth-token <token> static token auth (Authorization: <token>)\n"
            "      --auth-jwt-secret <s> JWT HS256 auth with shared secret\n"
            "      --auth-jwt-public-key <pem> JWT RS256 auth with PEM public key/cert\n"
            "                           (env fallbacks: MCP_AUTH_TOKEN/MCP_JWT_SECRET/\n"
            "                           MCP_JWT_PUBLIC_KEY; default: no auth)\n"
            "      --tls-cert <pem>     enable HTTPS with server cert (requires OpenSSL)\n"
            "      --tls-key <pem>      HTTPS private key\n",
            prog);
}

}  // namespace

/**
 * @brief 进程入口：解析参数 -> 初始化 provider -> 启动传输服务 -> 等待停机
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 0 正常退出；1 启动失败；2 参数错误
 *
 * 伪代码：
 *   1. 顺序扫描 argv 解析选项（-t/--transport、-p/--port、-b/--backend、
 *      --bind、--auth-token、--auth-jwt-secret、--auth-jwt-public-key、
 *      --tls-cert/--tls-key、-h/--help）；缺值或未知参数退出码 2；
 *   2. 由 backend 字符串与云端环境变量初始化工具层全局 provider；
 *   3. transport=stdio -> 直接运行 StdioServer 并以其返回码退出；
 *   4. HTTP 系传输 -> 构造 AuthChecker（JWT > token > 环境变量）->
 *      注册 MCP 路由 -> 按是否配置证书启动 HTTPS/HTTP -> 信号循环等待停机。
 */
int main(int argc, char** argv) {
    // 各配置项默认值
    std::string transport = "stdio";
    std::string backendStr = "adb";
    std::string bindAddr = "0.0.0.0";
    std::string authToken, jwtSecret, jwtKeyFile, tlsCert, tlsKey;
    int port = 8080;

    // 命令行参数解析：顺序扫描，遇带值选项用 next() 取下一个 argv
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        // 取下一个参数作为选项值；缺失则报错退出（退出码 2）
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                fprintf(stderr, "missing value for %s\n", name);
                exit(2);
            }
            return argv[++i];
        };
        if (arg == "-t" || arg == "--transport") {
            transport = next("transport");          // 传输方式：stdio|sse|streamable-http|http
        } else if (arg == "-p" || arg == "--port") {
            port = atoi(next("port").c_str());      // HTTP 监听端口
        } else if (arg == "-b" || arg == "--backend") {
            backendStr = next("backend");           // 默认执行后端：adb|cloud
        } else if (arg == "--bind") {
            bindAddr = next("bind");                // HTTP 绑定地址
        } else if (arg == "--auth-token") {
            authToken = next("auth-token");         // 静态 token 鉴权
        } else if (arg == "--auth-jwt-secret") {
            jwtSecret = next("auth-jwt-secret");    // JWT HS256 共享密钥
        } else if (arg == "--auth-jwt-public-key") {
            jwtKeyFile = next("auth-jwt-public-key"); // JWT RS256 公钥（PEM/URL）
        } else if (arg == "--tls-cert") {
            tlsCert = next("tls-cert");             // HTTPS 服务器证书
        } else if (arg == "--tls-key") {
            tlsKey = next("tls-key");               // HTTPS 私钥
        } else if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        } else {
            // 未知参数：打印用法并以退出码 2 拒绝启动
            fprintf(stderr, "unknown argument: %s\n", arg.c_str());
            usage(argv[0]);
            return 2;
        }
    }

    // 初始化工具层全局 provider：默认后端 + 来自环境变量的云端配置
    service::Backend backend = service::backendFromString(backendStr, service::Backend::Adb);
    tool::initProvider(backend, service::CloudConfig::fromEnv());

    // 忽略 SIGPIPE：避免对端断开 socket 时进程被信号杀死
    signal(SIGPIPE, SIG_IGN);

    fprintf(stderr, "mcp_mobile_use starting: transport=%s backend=%s\n", transport.c_str(),
            service::backendToString(backend).c_str());

    // stdio 传输：直接运行标准输入输出 MCP 服务，以其返回码作为进程退出码
    if (transport == "stdio") {
        mcp::StdioServer server;
        return server.run();
    }

    // 其余合法取值均为 HTTP 系传输；非法值报错退出
    if (transport != "sse" && transport != "streamable-http" && transport != "http") {
        fprintf(stderr, "invalid transport: %s\n", transport.c_str());
        usage(argv[0]);
        return 2;
    }

    net::HttpServer httpServer;
    mcp::McpHttpTransport mcpTransport;
    // auth 初始化：优先级 JWT > 静态 token > 环境变量 > 无鉴权
    mcp::AuthChecker auth = mcp::AuthChecker::none();
    if (!jwtSecret.empty() || !jwtKeyFile.empty()) {
        // JWT 模式：HS256 共享密钥与 RS256 公钥可单独或同时配置
        mcp::JwtVerifier verifier;
        if (!jwtSecret.empty()) verifier.setHs256Secret(jwtSecret);
        if (!jwtKeyFile.empty() && !loadRs256Key(verifier, jwtKeyFile)) return 1;
        auth = mcp::AuthChecker::withJwt(verifier);
    } else if (!authToken.empty()) {
        // 静态 token 模式：比对 Authorization 头
        auth = mcp::AuthChecker::withToken(authToken);
    } else {
        // 环境变量回退：MCP_AUTH_TOKEN / MCP_JWT_SECRET / MCP_JWT_PUBLIC_KEY
        auth = mcp::AuthChecker::fromEnv();
    }
    mcpTransport.setAuth(auth);
    mcpTransport.registerRoutes(httpServer);  // 注册 GET /sse、POST /message、POST /mcp

    // 启动监听：配置了证书则启用 HTTPS，否则普通 HTTP
    bool started = false;
    if (!tlsCert.empty() || !tlsKey.empty()) {
        started = httpServer.startTls(bindAddr, port, tlsCert, tlsKey);
        if (!started) fprintf(stderr, "failed to start https on %s:%d (check cert/key)\n",
                              bindAddr.c_str(), port);
    } else {
        started = httpServer.start(bindAddr, port);
        if (!started) fprintf(stderr, "failed to listen on %s:%d\n", bindAddr.c_str(), port);
    }
    if (!started) return 1;
    fprintf(stderr, "listening on %s:%d (%s; endpoints: GET /sse, POST /message, POST /mcp; "
                    "auth: %s)\n",
            bindAddr.c_str(), port, tlsCert.empty() ? "http" : "https", auth.describe().c_str());

    // 注册退出信号，200ms 轮询停机标志实现优雅停机
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    while (!gStop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(200));

    httpServer.stop();
    fprintf(stderr, "mcp_mobile_use stopped\n");
    return 0;
}
