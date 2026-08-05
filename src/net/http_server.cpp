/**
 * @file http_server.cpp
 * @brief net::HttpServer / Io / Request / Response 的实现——嵌入式 HTTP/HTTPS 服务器
 *
 * 功能：
 *   实现 Io 传输抽象（明文/TLS 双通道读写）、HTTP/1.1 请求报文解析、
 *   路由分发、keep-alive 连接复用、SSE 流式响应与可选 TLS 监听。
 *
 * 开发思路：
 *   1. 传输层：Io 的 read/writeAll 内部通过 #ifdef MCP_WITH_OPENSSL 判断
 *      ssl 指针是否有效，从而在"明文 recv/send"与"SSL_read/SSL_write"
 *      之间透明切换——这是对 OpenSSL TLS 的可选封装，上层零感知。
 *   2. 并发模型：thread-per-conn，每连接一个 detached 线程；连接线程
 *      首先 setsockopt(SO_RCVTIMEO) 设置 60 秒空闲超时（kIdleTimeoutSec），
 *      防止僵尸连接长期占用线程。
 *   3. 复用上限：单连接 keep-alive 最多处理 kMaxRequestsPerConn(100)
 *      个请求后主动关闭，防止连接被无限复用。
 *   4. 解析：readRequest 使用跨循环保留的 raw 缓冲区，先定位 "\r\n\r\n"
 *      头结束标记，再按 Content-Length 补齐请求体；上限 1MB 防头部轰炸。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "http_server.hpp"

#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef MCP_WITH_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace net {

namespace {

// keep-alive 双保险常量：限制单连接复用次数与空闲存活时间
const int kMaxRequestsPerConn = 100;   // 单连接最大请求数，防止无限复用
const int kIdleTimeoutSec = 60;        // keep-alive 空闲超时（秒），通过 SO_RCVTIMEO 生效

/**
 * @brief 字符串转小写（用于 HTTP 头字段名的大小写不敏感比较）
 * @param s 输入字符串
 * @return 小写副本
 */
std::string lower(const std::string& s) {
    std::string out = s;
    for (auto& c : out) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return out;
}

/**
 * @brief URL 百分号解码（%XX 还原为字节，'+' 还原为空格）
 * @param s 编码后的字符串
 * @return 解码结果；非法的 % 序列按原样保留
 *
 * 实现思路（伪代码）：
 *   for 每个字符 c:
 *       若 c=='%' 且其后两位都是合法十六进制 -> 合成一个字节，跳过 3 个字符
 *       否则 c=='+' -> 输出空格
 *       否则 -> 原样输出
 */
std::string urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            // 单字符十六进制值解析：0-9 / a-f / A-F，非法返回 -1
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);  // 两个 hex 位拼成一个字节
                i += 2;
                continue;
            }
        }
        out += s[i] == '+' ? ' ' : s[i];
    }
    return out;
}

}  // namespace

/**
 * @brief Io::read 实现——TLS 可选封装的读路径
 *
 * 实现思路：
 *   编译了 OpenSSL 且 ssl 非空 -> SSL_read（解密读取）；
 *   否则 -> recv（明文读取）。返回值语义两者一致：>0 为字节数，<=0 为关闭/出错。
 */
int Io::read(void* buf, size_t len) {
#ifdef MCP_WITH_OPENSSL
    if (ssl) return SSL_read(ssl, buf, static_cast<int>(len));
#endif
    return recv(fd, buf, len, 0);
}

/**
 * @brief Io::writeAll 实现——循环写出直到全部发送完毕
 * @param data 数据指针
 * @param len 数据长度
 * @return 全部写出返回 true，任一次写返回 <=0 视为失败返回 false
 *
 * 实现思路（伪代码）：
 *   sent = 0
 *   while sent < len:
 *       若 TLS 模式: w = SSL_write(ssl, data+sent, len-sent)
 *       否则:        w = send(fd, data+sent, len-sent, MSG_NOSIGNAL)
 *       w <= 0 -> return false   // 对端关闭或超时
 *       sent += w                // 处理短写
 *   return true
 */
bool Io::writeAll(const void* data, size_t len) {
    const char* p = static_cast<const char*>(data);
    size_t sent = 0;
    while (sent < len) {
#ifdef MCP_WITH_OPENSSL
        if (ssl) {
            int w = SSL_write(ssl, p + sent, static_cast<int>(len - sent));
            if (w <= 0) return false;
            sent += static_cast<size_t>(w);
            continue;
        }
#endif
        // MSG_NOSIGNAL：避免对端关闭时进程收到 SIGPIPE 被终止
        ssize_t w = send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (w <= 0) return false;
        sent += static_cast<size_t>(w);
    }
    return true;
}

/** @brief writeAll 的 string 重载：直接转发到 (data, size) 版本 */
bool Io::writeAll(const std::string& s) { return writeAll(s.data(), s.size()); }

/**
 * @brief Io::close 实现——按正确顺序释放 TLS 会话与 socket，且幂等
 *
 * 实现思路：
 *   先 SSL_shutdown（发送 close_notify）+ SSL_free 并清空指针，
 *   再关闭 fd 并置 -1；两个分支都有空值保护，重复调用安全。
 */
void Io::close() {
#ifdef MCP_WITH_OPENSSL
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
        ssl = nullptr;
    }
#endif
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

/**
 * @brief 大小写不敏感查找请求头
 * @param name 头字段名
 * @return 找到返回值，未找到返回空串
 *
 * 实现思路：将目标名与每个头的字段名都转小写后逐一比较，返回首个匹配。
 */
std::string Request::header(const std::string& name) const {
    std::string lname = lower(name);
    for (const auto& h : headers)
        if (lower(h.first) == lname) return h.second;
    return "";
}

/**
 * @brief 从查询串中按名取参数值（自动 URL 解码）
 * @param name 参数名
 * @return 解码后的值；不存在或无等号时返回空串
 *
 * 实现思路（伪代码）：
 *   pos = 0
 *   while pos <= query.size():
 *       截取 [pos, 下一个'&') 得到 pair
 *       在 pair 中找 '='：有 -> key=等号前, value=urlDecode(等号后)
 *                        无 -> key=整个 pair, value=""
 *       key == name -> return value
 *       pos 移到 '&' 之后
 *   return ""
 */
std::string Request::queryParam(const std::string& name) const {
    size_t pos = 0;
    while (pos <= query.size()) {
        size_t amp = query.find('&', pos);
        if (amp == std::string::npos) amp = query.size();
        std::string pair = query.substr(pos, amp - pos);
        size_t eq = pair.find('=');
        std::string key = eq == std::string::npos ? pair : pair.substr(0, eq);
        if (key == name) return eq == std::string::npos ? "" : urlDecode(pair.substr(eq + 1));
        pos = amp + 1;
    }
    return "";
}

/** @brief 构造 JSON 响应 */
Response Response::json(int status, const std::string& body) {
    Response r;
    r.status = status;
    r.contentType = "application/json";
    r.body = body;
    return r;
}

/** @brief 构造纯文本响应 */
Response Response::text(int status, const std::string& body) {
    Response r;
    r.status = status;
    r.contentType = "text/plain";
    r.body = body;
    return r;
}

/** @brief 析构：确保 stop() 被调用，回收线程与 socket 资源 */
HttpServer::~HttpServer() { stop(); }

/**
 * @brief 注册路由
 * @param method HTTP 方法
 * @param path 路径
 * @param handler 处理回调（移动语义存入路由表）
 */
void HttpServer::addRoute(const std::string& method, const std::string& path, Handler handler) {
    m_routes.push_back({method, path, std::move(handler)});
}

/**
 * @brief 线性查找路由
 * @return 命中返回 Handler 指针，未命中返回 nullptr
 *
 * 实现思路：顺序扫描 m_routes，method 与 path 均精确相等即命中。
 * MCP 服务路由数量极少（个位数），线性扫描足够且保持精确匹配语义。
 */
const HttpServer::Handler* HttpServer::findRoute(const std::string& method,
                                                 const std::string& path) const {
    for (const auto& r : m_routes)
        if (r.method == method && r.path == path) return &r.handler;
    return nullptr;
}

/** @brief 明文启动：直接委托给 startTls，cert/key 传空表示不启用 TLS */
bool HttpServer::start(const std::string& addr, int port) {
    return startTls(addr, port, "", "");
}

/**
 * @brief 启动服务器（可选 TLS）
 * @param addr 绑定 IP（点分十进制）
 * @param port 监听端口
 * @param certFile 证书链 PEM 路径（与 keyFile 均空则明文 HTTP）
 * @param keyFile 私钥 PEM 路径
 * @return 成功返回 true；任何一步失败返回 false 且不残留资源
 *
 * 实现思路（伪代码）：
 *   wantTls = cert 或 key 非空
 *   若 wantTls:
 *       有 OpenSSL -> 建 SSL_CTX，最低 TLS1.2，加载证书链/私钥并校验匹配，失败即清理返回
 *       无 OpenSSL -> 直接返回 false（降级：不支持 TLS）
 *   socket() -> SO_REUSEADDR -> inet_pton 解析地址 -> bind -> listen(16)
 *   任一步失败 -> close 并返回 false
 *   置 m_running，启动 accept 线程
 */
bool HttpServer::startTls(const std::string& addr, int port, const std::string& certFile,
                          const std::string& keyFile) {
    bool wantTls = !certFile.empty() || !keyFile.empty();
#ifdef MCP_WITH_OPENSSL
    if (wantTls) {
        // 初始化 TLS 服务端上下文：通用 TLS 方法，最低协议版本 TLS1.2
        m_sslCtx = SSL_CTX_new(TLS_server_method());
        if (!m_sslCtx) return false;
        SSL_CTX_set_min_proto_version(m_sslCtx, TLS1_2_VERSION);
        // 加载证书链与私钥，并校验二者匹配；任一失败即释放上下文返回
        if (SSL_CTX_use_certificate_chain_file(m_sslCtx, certFile.c_str()) != 1 ||
            SSL_CTX_use_PrivateKey_file(m_sslCtx, keyFile.c_str(), SSL_FILETYPE_PEM) != 1 ||
            SSL_CTX_check_private_key(m_sslCtx) != 1) {
            SSL_CTX_free(m_sslCtx);
            m_sslCtx = nullptr;
            return false;
        }
    }
#else
    // 降级逻辑：未编译 OpenSSL 时无法提供 TLS，请求 HTTPS 直接失败
    if (wantTls) return false;  // 未编译 OpenSSL，不支持 TLS
#endif

    m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenFd < 0) return false;
    int opt = 1;
    // SO_REUSEADDR：允许服务器重启后立即绑定同一端口（跳过 TIME_WAIT 等待）
    setsockopt(m_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(static_cast<uint16_t>(port));
    // 解析地址、绑定、监听（backlog=16）；任一失败则清理 fd 返回
    if (inet_pton(AF_INET, addr.c_str(), &sa.sin_addr) != 1 ||
        bind(m_listenFd, reinterpret_cast<struct sockaddr*>(&sa), sizeof(sa)) != 0 ||
        listen(m_listenFd, 16) != 0) {
        close(m_listenFd);
        m_listenFd = -1;
        return false;
    }

    m_port = port;
    m_stop = false;
    m_running = true;
    m_acceptThread = std::thread(&HttpServer::acceptLoop, this);  // 启动 accept 线程
    return true;
}

/**
 * @brief 停止服务器
 *
 * 实现思路：
 *   1. m_running 由 true 换为 false，若已是 false（未运行/已停止）直接返回，保证幂等；
 *   2. 置 m_stop 标志，再 shutdown+close 监听 fd——使阻塞在 accept() 的
 *      acceptLoop 立即返回错误并因 m_stop 置位而退出循环；
 *   3. join accept 线程，最后释放 SSL_CTX。
 *   注意：已建立的连接线程是 detached 的，由各自的读超时（60s SO_RCVTIMEO）
 *   与 m_stop 检查自然退出。
 */
void HttpServer::stop() {
    if (!m_running.exchange(false)) return;
    m_stop = true;
    if (m_listenFd >= 0) {
        shutdown(m_listenFd, SHUT_RDWR);
        close(m_listenFd);
        m_listenFd = -1;
    }
    if (m_acceptThread.joinable()) m_acceptThread.join();
#ifdef MCP_WITH_OPENSSL
    if (m_sslCtx) {
        SSL_CTX_free(m_sslCtx);
        m_sslCtx = nullptr;
    }
#endif
}

/**
 * @brief accept 线程主循环——thread-per-conn 模型的入口
 *
 * 实现思路（伪代码）：
 *   while 未停止:
 *       fd = accept(m_listenFd)          // 阻塞等待新连接
 *       fd < 0: 若因 stop 退出则 break，否则 continue
 *       detach 新线程处理该连接:
 *           设置 60 秒 SO_RCVTIMEO 空闲超时（kIdleTimeoutSec）：
 *             keep-alive 连接两次请求之间若空闲超过 60 秒，recv 超时返回，
 *             线程随之退出，防止僵尸连接常驻
 *           若启用 TLS: SSL_new + SSL_set_fd + SSL_accept 完成握手，失败即关闭
 *           handleConnection(io)         // 进入 keep-alive 请求循环
 *           io.close()                   // 线程结束前释放连接资源
 */
void HttpServer::acceptLoop() {
    while (!m_stop.load()) {
        int fd = accept(m_listenFd, nullptr, nullptr);
        if (fd < 0) {
            if (m_stop.load()) break;  // stop() 关闭了监听 fd，正常退出
            continue;
        }
        // thread-per-conn：每个连接一个 detached 线程，互不阻塞
        std::thread([this, fd] {
            Io io;
            io.fd = fd;
            // 60 秒空闲 SO_RCVTIMEO：作用于该连接所有后续 recv/SSL_read
            struct timeval tv;
            tv.tv_sec = kIdleTimeoutSec;
            tv.tv_usec = 0;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#ifdef MCP_WITH_OPENSSL
            // 若服务器以 TLS 模式启动，则在此完成服务端握手，把 io 升级为加密通道
            if (m_sslCtx) {
                io.ssl = SSL_new(m_sslCtx);
                if (!io.ssl) {
                    io.close();
                    return;
                }
                SSL_set_fd(io.ssl, fd);
                if (SSL_accept(io.ssl) != 1) {
                    io.close();
                    return;
                }
            }
#endif
            handleConnection(io);
            io.close();
        }).detach();
    }
}

/**
 * @brief 单连接 keep-alive 请求处理循环
 * @param io 已就绪连接（明文或已完成 TLS 握手）
 *
 * 实现思路（伪代码）：
 *   raw 缓冲区跨请求保留（keep-alive 下可能预读到下一请求的字节）
 *   requestCount = 0
 *   while 未停止:
 *       readRequest 解析一个完整请求，失败（关闭/超时/畸形）-> return
 *       requestCount++
 *       查路由：未命中 -> 写 404 JSON 并关闭连接 return
 *       调用 handler（捕获异常 -> 500 JSON）
 *       若 SSE 响应: 写 SSE 响应头 -> 移交 sseHandler 持续推送 -> return（连接随回调结束关闭）
 *       keepAlive = 客户端未要求 close 且 requestCount < 100（kMaxRequestsPerConn）
 *       拼响应头（状态行/Content-Type/Content-Length/附加头/Connection）并连同 body 写出
 *       写失败或不再 keep-alive -> return
 */
void HttpServer::handleConnection(Io& io) {
    std::string raw;
    raw.reserve(4096);
    int requestCount = 0;

    while (!m_stop.load()) {
        Request req;
        if (!readRequest(io, raw, req)) return;

        requestCount++;
        const Handler* handler = findRoute(req.method, req.path);
        if (!handler) {
            // 路由未命中：返回 404 JSON，并以 Connection: close 结束连接
            Response r = Response::json(404, "{\"error\":\"not found\"}");
            io.writeAll("HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\n"
                        "Content-Length: " + std::to_string(r.body.size()) +
                        "\r\nConnection: close\r\n\r\n");
            io.writeAll(r.body);
            return;
        }

        Response resp;
        try {
            resp = (*handler)(req);
        } catch (const std::exception& e) {
            // 业务异常兜底为 500，避免连接线程崩溃
            resp = Response::json(500, std::string("{\"error\":\"") + e.what() + "\"}");
        }

        // SSE 流式响应：框架只负责写响应头，之后把连接交给 sseHandler 持续推送
        if (resp.sse) {
            io.writeAll("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                        "Cache-Control: no-cache\r\nConnection: keep-alive\r\n"
                        "Access-Control-Allow-Origin: *\r\n\r\n");
            if (resp.sseHandler) resp.sseHandler(io);
            return;
        }

        // keep-alive 判定：客户端未要求 close，且未达单连接 100 请求上限
        bool keepAlive = req.header("Connection") != "close" && requestCount < kMaxRequestsPerConn;
        std::string reason = resp.status == 200 ? "OK" : (resp.status == 202 ? "Accepted" : "Error");
        std::string head = "HTTP/1.1 " + std::to_string(resp.status) + " " + reason + "\r\n";
        head += "Content-Type: " + resp.contentType + "\r\n";
        head += "Content-Length: " + std::to_string(resp.body.size()) + "\r\n";
        for (const auto& h : resp.extraHeaders) head += h.first + ": " + h.second + "\r\n";
        head += keepAlive ? "Connection: keep-alive\r\n\r\n" : "Connection: close\r\n\r\n";
        if (!io.writeAll(head) || !io.writeAll(resp.body)) return;
        if (!keepAlive) return;
    }
}

/**
 * @brief 从连接读取并解析一个完整 HTTP 请求
 * @param io 连接
 * @param raw 跨请求复用的读缓冲（可能含有上一次多读的字节，属于下一请求）
 * @param req 输出：解析结果
 * @return 成功 true；连接关闭/读超时/报文超限或畸形 false
 *
 * 实现思路（伪代码）：
 *   1. 循环读数据直到 raw 中出现 "\r\n\r\n"（头部结束）；
 *      raw 超过 1MB 仍未出现 -> 判定畸形，防头部轰炸
 *   2. 解析请求行 "METHOD target HTTP/1.1"：
 *      按两个空格切出 method 与 target；target 按 '?' 拆为 path 与 query
 *   3. 逐行解析头部直到 headerEnd：按 ':' 拆键值，去掉值前导空白
 *   4. 读 Content-Length，循环补读直到 body 完整
 *   5. 截取 body 并从 raw 头部 erase 已消费字节，残留留给下一请求
 */
bool HttpServer::readRequest(Io& io, std::string& raw, Request& req) {
    // 第一步：读到头部结束标记为止；上限 1MB 防止恶意超长头部耗尽内存
    size_t headerEnd = std::string::npos;
    while (raw.size() < 1024 * 1024) {
        headerEnd = raw.find("\r\n\r\n");
        if (headerEnd != std::string::npos) break;
        char buf[8192];
        ssize_t r = io.read(buf, sizeof(buf));
        if (r <= 0) return false;  // 对端关闭或 60 秒空闲超时
        raw.append(buf, static_cast<size_t>(r));
    }
    if (headerEnd == std::string::npos) return false;

    // 第二步：解析请求行，切出 method 与 target，再按 '?' 拆 path/query
    size_t lineEnd = raw.find("\r\n");
    std::string requestLine = raw.substr(0, lineEnd);
    size_t sp1 = requestLine.find(' ');
    size_t sp2 = requestLine.find(' ', sp1 == std::string::npos ? sp1 : sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) return false;
    req.method = requestLine.substr(0, sp1);
    std::string target = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
    size_t q = target.find('?');
    if (q == std::string::npos) {
        req.path = target;
    } else {
        req.path = target.substr(0, q);
        req.query = target.substr(q + 1);
    }

    // 第三步：逐行解析头部（保留原始顺序存入 vector），值去掉前导空格/制表符
    size_t pos = lineEnd + 2;
    while (pos < headerEnd) {
        size_t end = raw.find("\r\n", pos);
        if (end == std::string::npos || end > headerEnd) break;
        std::string line = raw.substr(pos, end - pos);
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string name = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) value.erase(0, 1);
            req.headers.emplace_back(name, value);
        }
        pos = end + 2;
    }

    // 第四步：按 Content-Length 确定 body 长度（无该头则按 0 处理）
    size_t contentLength = 0;
    std::string cl = req.header("Content-Length");
    if (!cl.empty()) contentLength = static_cast<size_t>(atol(cl.c_str()));

    // 第五步：补读至 body 完整，然后截取 body 并把已消费字节从缓冲中移除
    size_t bodyStart = headerEnd + 4;
    while (raw.size() < bodyStart + contentLength) {
        char buf[8192];
        ssize_t r = io.read(buf, sizeof(buf));
        if (r <= 0) return false;
        raw.append(buf, static_cast<size_t>(r));
    }
    req.body = raw.substr(bodyStart, contentLength);
    raw.erase(0, bodyStart + contentLength);  // 残留字节属于 keep-alive 的下一请求
    return true;
}

}  // namespace net
