/**
 * @file http_server.hpp
 * @brief 轻量级嵌入式 HTTP/HTTPS 服务器（net::HttpServer）——MCP 服务的入口组件
 *
 * 功能：
 *   提供基于路由表的 HTTP/1.1 服务器：注册 method+path 到 Handler 的映射，
 *   支持 keep-alive 连接复用、SSE（Server-Sent Events）流式响应，
 *   以及在编译期启用 MCP_WITH_OPENSSL 时的 TLS（HTTPS）监听。
 *
 * 开发思路：
 *   1. 为满足"零第三方依赖"约束，不引入 boost::asio / civetweb 等框架，
 *      直接基于 POSIX socket 手写 accept 循环与 HTTP 报文解析。
 *   2. 抽象出 Io 结构体统一"明文 fd"与"TLS SSL*"两种传输通道，
 *      上层的请求解析、响应写回逻辑完全不感知 TLS 是否存在，
 *      TLS 支持通过 #ifdef MCP_WITH_OPENSSL 在编译期裁剪。
 *   3. 采用 thread-per-conn 模型：每个连接一个 detached 线程，
 *      MCP 场景并发量低（通常个位数连接），用线程换取代码简洁与
 *      SSE 长连接的天然阻塞式写法，避免引入事件循环的复杂度。
 *   4. keep-alive 复用设置双重保险：单连接最多 100 个请求
 *      （kMaxRequestsPerConn）防无限复用，SO_RCVTIMEO 60 秒空闲超时
 *      防止僵尸连接占用线程。
 *
 * @author hubin
 * @date 2026-08-05
 */
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

// 仅前置声明 OpenSSL 类型，避免头文件依赖 <openssl/ssl.h>，
// 使得未启用 TLS 的编译单元包含本头文件时无需 OpenSSL 头文件
#ifdef MCP_WITH_OPENSSL
struct ssl_st;
typedef struct ssl_st SSL;
struct ssl_ctx_st;
typedef struct ssl_ctx_st SSL_CTX;
#endif

namespace net {

/**
 * @struct Io
 * @brief 统一读写抽象层：对上层屏蔽"明文 socket fd"与"OpenSSL TLS SSL*"的差异
 *
 * 开发思路：
 *   连接建立后，后续所有读写只通过 Io::read / Io::writeAll / Io::close 三个接口。
 *   若编译了 OpenSSL 且该连接已完成 TLS 握手，则 ssl 非空，读写走
 *   SSL_read/SSL_write；否则退化为 recv/send。close() 负责按正确顺序
 *   释放 SSL 对象与 fd，避免上层遗漏资源释放。
 */
struct Io {
    int fd = -1;  ///< 底层 socket 文件描述符（明文模式唯一通道）
#ifdef MCP_WITH_OPENSSL
    SSL* ssl = nullptr;  ///< TLS 会话对象，非空表示走加密通道（可选封装）
#endif

    /**
     * @brief 从连接读取数据（TLS 模式走 SSL_read，明文走 recv）
     * @param buf 接收缓冲区
     * @param len 缓冲区长度
     * @return 实际读取字节数；<=0 表示对端关闭或出错
     */
    int read(void* buf, size_t len);

    /**
     * @brief 完整写出 len 字节（循环处理短写）
     * @param data 数据指针
     * @param len 数据长度
     * @return 全部写出成功返回 true，写失败返回 false
     */
    bool writeAll(const void* data, size_t len);

    /** @brief writeAll 的 std::string 便捷重载 */
    bool writeAll(const std::string& s);

    /** @brief 关闭连接：先 SSL_shutdown/SSL_free，再 close(fd)，幂等安全 */
    void close();
};

/**
 * @struct Request
 * @brief 解析后的 HTTP 请求：方法、路径、查询串、头部列表、请求体
 *
 * 开发思路：
 *   headers 用 vector<pair> 而非 map，以保留原始顺序且允许同名头重复出现；
 *   查询时通过 header() 做大小写不敏感查找（HTTP 头字段名不区分大小写）。
 */
struct Request {
    std::string method;                                         ///< 请求方法，如 GET/POST
    std::string path;                                           ///< 路径部分（不含 ? 查询串）
    std::string query;                                          ///< 原始查询串（? 之后内容，未解码）
    std::vector<std::pair<std::string, std::string>> headers;   ///< 头部键值对（保留顺序）
    std::string body;                                           ///< 请求体（按 Content-Length 截取）

    /**
     * @brief 大小写不敏感地查找请求头
     * @param name 头字段名（任意大小写）
     * @return 找到返回值，未找到返回空字符串
     */
    std::string header(const std::string& name) const;

    /**
     * @brief 从查询串中取参数值（自动 URL 解码）
     * @param name 参数名
     * @return 参数值（已解码）；无等号的裸键返回空串，不存在也返回空串
     */
    std::string queryParam(const std::string& name) const;
};

/**
 * @struct Response
 * @brief Handler 返回的 HTTP 响应：状态码、内容类型、附加头、正文；可选 SSE 模式
 *
 * 开发思路：
 *   普通响应由框架统一序列化（Content-Length 等头部自动补齐）；
 *   SSE 响应只需置 sse=true 并提供 sseHandler 回调，框架写好 SSE 响应头后
 *   把 Io 交给回调持续推送事件流，回调返回即关闭连接。
 */
struct Response {
    int status = 200;                                        ///< HTTP 状态码
    std::string contentType = "application/json";            ///< Content-Type
    std::map<std::string, std::string> extraHeaders;         ///< 附加响应头
    std::string body;                                        ///< 响应正文

    bool sse = false;                        ///< true 表示本响应为 SSE 流
    std::function<void(Io&)> sseHandler;     ///< SSE 推送回调，参数为连接 Io

    /** @brief 构造 JSON 响应（Content-Type: application/json） */
    static Response json(int status, const std::string& body);
    /** @brief 构造纯文本响应（Content-Type: text/plain） */
    static Response text(int status, const std::string& body);
};

/**
 * @class HttpServer
 * @brief 嵌入式 HTTP/HTTPS 服务器：路由分发 + keep-alive + SSE + 可选 TLS
 *
 * 开发思路：
 *   1. start()/startTls() 完成 TLS 上下文初始化（可选）、socket 创建、
 *      bind/listen 后，启动一个 accept 线程进入 acceptLoop()。
 *   2. acceptLoop 每 accept 到一个连接就 detach 一个新线程处理
 *      （thread-per-conn 模型），线程内先设置 60 秒空闲 SO_RCVTIMEO，
 *      若启用 TLS 则完成握手，再进入 handleConnection 的请求循环。
 *   3. handleConnection 循环调用 readRequest 解析请求，查路由表分发，
 *      单连接最多处理 kMaxRequestsPerConn(100) 个请求后主动关闭，
 *      防止连接被无限复用导致线程常驻。
 *   4. stop() 通过 shutdown+close 监听 fd 使 accept 返回错误退出循环，
 *      再 join accept 线程并释放 SSL_CTX，保证析构安全。
 */
class HttpServer {
public:
    using Handler = std::function<Response(const Request&)>;  ///< 路由处理函数类型

    /** @brief 析构时自动 stop()，确保线程与 socket 资源释放 */
    ~HttpServer();

    /**
     * @brief 注册路由（精确匹配 method + path）
     * @param method HTTP 方法，如 "GET"/"POST"
     * @param path 路径，如 "/mcp"
     * @param handler 处理回调
     */
    void addRoute(const std::string& method, const std::string& path, Handler handler);

    /**
     * @brief 启动明文 HTTP 监听（等价于 startTls(addr, port, "", "")）
     * @param addr 绑定 IP（点分十进制，如 "127.0.0.1"）
     * @param port 监听端口
     * @return 成功返回 true
     */
    bool start(const std::string& addr, int port);

    /**
     * @brief 启动监听，可选启用 HTTPS：cert/key 为 PEM 文件路径（需 MCP_WITH_OPENSSL 构建）
     * @param addr 绑定 IP
     * @param port 监听端口
     * @param certFile 证书链 PEM 路径（为空则表示明文 HTTP）
     * @param keyFile 私钥 PEM 路径
     * @return 成功返回 true；请求 TLS 但未编译 OpenSSL 时返回 false
     */
    bool startTls(const std::string& addr, int port, const std::string& certFile,
                  const std::string& keyFile);

    /** @brief 停止服务器：关闭监听 fd、退出并回收 accept 线程、释放 TLS 上下文 */
    void stop();

    bool running() const { return m_running; }  ///< 是否正在运行
    int port() const { return m_port; }         ///< 当前监听端口

private:
    /** @brief accept 线程主循环：接受连接并为每个连接派生处理线程 */
    void acceptLoop();

    /**
     * @brief 单连接处理循环：反复解析请求、分发路由、写回响应（keep-alive）
     * @param io 已就绪的连接（明文或已完成 TLS 握手）
     */
    void handleConnection(Io& io);

    /**
     * @brief 从连接读取并解析一个完整 HTTP 请求
     * @param io 连接
     * @param buffer 跨请求复用的读缓冲（keep-alive 下可能残留下一请求的头部字节）
     * @param req 输出参数，解析结果
     * @return 解析成功返回 true；连接关闭/超时/报文畸形返回 false
     */
    bool readRequest(Io& io, std::string& buffer, Request& req);

    /**
     * @brief 查找路由（线性扫描，路由数量极少，无需哈希表）
     * @return 命中返回 Handler 指针，未命中返回 nullptr
     */
    const Handler* findRoute(const std::string& method, const std::string& path) const;

    bool shouldStop() const { return m_stop.load(); }  ///< 查询停止标志

    int m_listenFd = -1;              ///< 监听 socket
    int m_port = 0;                   ///< 监听端口
    std::atomic<bool> m_stop{false};    ///< 停止标志
    std::atomic<bool> m_running{false}; ///< 运行标志
    std::thread m_acceptThread;       ///< accept 线程
#ifdef MCP_WITH_OPENSSL
    SSL_CTX* m_sslCtx = nullptr;      ///< TLS 服务端上下文（明文模式为 nullptr）
#endif

    /** @brief 路由表项：方法 + 路径 + 处理器 */
    struct Route {
        std::string method;
        std::string path;
        Handler handler;
    };
    std::vector<Route> m_routes;  ///< 路由表
};

}  // namespace net
