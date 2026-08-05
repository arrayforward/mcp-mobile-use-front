/**
 * @file https_client.cpp
 * @brief net::HttpsClient / fetchUrl 的实现——HTTPS(TLS) 与明文 HTTP 客户端
 *
 * 功能：
 *   实现 connectTcp（带超时的 TCP 建连）、HttpsClient::request（TLS 握手 +
 *   HTTP/1.1 请求收发）、httpGetPlain（明文 GET）、parseUrl（URL 解析）
 *   以及统一入口 fetchUrl 的 http/https 分流。
 *
 * 开发思路：
 *   1. 文件整体按 #ifdef MCP_WITH_OPENSSL 分为两个实现分支：
 *      - 开启：完整实现 HTTPS（OpenSSL）+ 明文 HTTP，fetchUrl 两者皆支持；
 *      - 未开启（#else 分支）：降级实现——HttpsClient::post/get 直接返回
 *        "not compiled in" 错误，fetchUrl 仅支持 http:// 明文拉取，
 *        https:// 返回明确错误提示。两个分支中 connectTcp / parseUrl /
 *        httpGetPlain 的代码保持一致，便于对照维护。
 *   2. TLS 客户端配置从简：TLS_client_method + SSL_VERIFY_NONE（不校验
 *      服务端证书链），但设置 SNI（SSL_set_tlsext_host_name）以兼容
 *      基于域名的虚拟主机；本组件用于拉取公开资源，可接受该安全取舍。
 *   3. 请求均为 Connection: close 短连接：写完请求后持续 SSL_read/recv
 *      直到对端关闭，再以 "\r\n\r\n" 切分响应头与正文。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "https_client.hpp"

#include <string.h>

#include <chrono>

#ifdef MCP_WITH_OPENSSL

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

namespace net {

namespace {

/**
 * @brief 建立带超时的 TCP 连接（IPv4/IPv6 均可）
 * @param host 主机名或 IP
 * @param port 端口
 * @param timeoutMs 超时（毫秒），同时作为收发 SO_RCVTIMEO/SO_SNDTIMEO
 * @param err 输出：失败原因
 * @return 成功返回 fd，失败返回 -1
 *
 * 实现思路（伪代码）：
 *   getaddrinfo 解析主机（AF_UNSPEC，IPv4/IPv6 不限）
 *   for 每个候选地址:
 *       socket() -> 设置收/发超时 -> connect()
 *       成功 -> break 返回 fd；失败 -> close 尝试下一个
 *   全部失败 -> 填 err 返回 -1
 */
int connectTcp(const std::string& host, int port, int timeoutMs, std::string& err) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        err = "getaddrinfo failed for " + host;
        return -1;
    }

    int fd = -1;
    // 依次尝试每个解析结果：socket -> 设超时 -> connect，失败则换下一个
    for (struct addrinfo* p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;

        // 将超时换算为 timeval，同时作用于后续 recv 与 send
        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) err = "connect failed to " + host + ":" + portStr;
    return fd;
}

/**
 * @brief 循环 SSL_read 直到对端关闭，把全部响应数据追加到 out
 * @param ssl 已完成握手的 TLS 会话
 * @param out 输出：累计数据
 * @return 正常读到 close_notify 返回 true；协议错误返回 false
 *
 * 实现思路（伪代码）：
 *   loop:
 *       r = SSL_read(...)
 *       r > 0   -> 追加数据，继续
 *       ZERO_RETURN      -> 对端正常关闭（Connection: close），成功返回
 *       WANT_READ/WRITE  -> 非致命（含超时重试场景），继续循环
 *       其他错误          -> 返回 false
 */
bool readAll(SSL* ssl, std::string& out) {
    char buf[16384];
    while (true) {
        int r = SSL_read(ssl, buf, sizeof(buf));
        if (r > 0) {
            out.append(buf, static_cast<size_t>(r));
            continue;
        }
        int code = SSL_get_error(ssl, r);
        if (code == SSL_ERROR_ZERO_RETURN) return true;
        if (code == SSL_ERROR_WANT_READ || code == SSL_ERROR_WANT_WRITE) continue;
        return false;
    }
}

}  // namespace

/** @brief 本编译分支内置 OpenSSL，HTTPS 客户端可用 */
bool httpsClientAvailable() { return true; }

/** @brief HTTPS POST：委托 request("POST", ...) */
HttpResponse HttpsClient::post(const std::string& host, int port, const std::string& path,
                               const std::map<std::string, std::string>& headers,
                               const std::string& body) {
    return request("POST", host, port, path, headers, body);
}

/** @brief HTTPS GET：委托 request("GET", ...)，无附加头与请求体 */
HttpResponse HttpsClient::get(const std::string& host, int port, const std::string& path) {
    return request("GET", host, port, path, {}, "");
}

/**
 * @brief HTTPS 请求主流程
 * @param method HTTP 方法
 * @param host 主机名
 * @param port 端口
 * @param path 路径
 * @param headers 附加请求头
 * @param body 请求体（可为空）
 * @return 响应；任一步失败提前返回且 error 非空
 *
 * 实现思路（伪代码）：
 *   fd = connectTcp(host, port, timeout)          // 带超时的 TCP 建连
 *   ctx = SSL_CTX_new(TLS_client_method)          // 客户端 TLS 上下文
 *   SSL_CTX_set_verify(NONE)                      // 不校验证书链（拉公开资源）
 *   ssl = SSL_new(ctx); SSL_set_fd; 设置 SNI 主机名
 *   SSL_connect 握手，失败即清理返回
 *   拼 HTTP/1.1 请求（Host / Content-Length / Connection: close / 附加头 / body）
 *   循环 SSL_write 写完全部请求
 *   readAll 读完整个响应（对端关闭为止）
 *   SSL_shutdown + 释放 ssl/ctx/fd
 *   解析状态行（sscanf "HTTP/%*s %d"）与 "\r\n\r\n" 之后的 body
 */
HttpResponse HttpsClient::request(const std::string& method, const std::string& host, int port,
                                  const std::string& path,
                                  const std::map<std::string, std::string>& headers,
                                  const std::string& body) {
    HttpResponse resp;

    // 第一步：TCP 建连（带超时）
    std::string err;
    int fd = connectTcp(host, port, m_timeoutMs, err);
    if (fd < 0) {
        resp.error = err;
        return resp;
    }

    // 第二步：创建客户端 TLS 上下文；不校验服务端证书（VERIFY_NONE）
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        close(fd);
        resp.error = "SSL_CTX_new failed";
        return resp;
    }
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

    // 第三步：创建 TLS 会话、绑定 fd、设置 SNI（虚拟主机必需）
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, host.c_str());

    // 第四步：TLS 握手
    if (SSL_connect(ssl) != 1) {
        resp.error = "TLS handshake failed with " + host;
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(fd);
        return resp;
    }

    // 第五步：拼装 HTTP/1.1 请求报文（Connection: close，短连接）
    std::string req = method + " " + path + " HTTP/1.1\r\n";
    req += "Host: " + host + "\r\n";
    if (!body.empty()) req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    req += "Connection: close\r\n";
    for (const auto& h : headers) req += h.first + ": " + h.second + "\r\n";
    req += "\r\n";
    req += body;

    // 第六步：循环写出（处理短写）
    size_t sent = 0;
    while (sent < req.size()) {
        int w = SSL_write(ssl, req.data() + sent, static_cast<int>(req.size() - sent));
        if (w <= 0) {
            resp.error = "SSL_write failed";
            SSL_free(ssl);
            SSL_CTX_free(ctx);
            close(fd);
            return resp;
        }
        sent += static_cast<size_t>(w);
    }

    // 第七步：读到对端关闭为止，拿到完整响应
    std::string raw;
    if (!readAll(ssl, raw)) {
        resp.error = "failed reading response";
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        close(fd);
        return resp;
    }

    // 释放 TLS 与 socket 资源
    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    close(fd);

    // 第八步：解析状态行
    size_t lineEnd = raw.find("\r\n");
    if (lineEnd == std::string::npos) {
        resp.error = "malformed http response";
        return resp;
    }
    if (sscanf(raw.c_str(), "HTTP/%*s %d", &resp.status) != 1) {
        resp.error = "malformed status line: " + raw.substr(0, lineEnd);
        return resp;
    }

    // 第九步：按 "\r\n\r\n" 切出 body（不解析 chunked，依赖短连接一次性收完）
    size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        resp.error = "malformed http headers";
        return resp;
    }
    resp.body = raw.substr(headerEnd + 4);
    return resp;
}

namespace {

/**
 * @brief 明文 HTTP GET（socket），供无 TLS 场景或 fetchUrl 的 http:// 分流使用
 * @param host 主机名
 * @param port 端口
 * @param path 路径
 * @param timeoutMs 超时（毫秒）
 * @return 响应；失败时 error 非空
 *
 * 实现思路：connectTcp 建连 -> 拼 GET 请求（Connection: close）->
 * 循环 send -> 循环 recv 直到对端关闭 -> 解析状态行与 body。
 * 与 TLS 版本结构完全对称，仅把 SSL_write/SSL_read 换成 send/recv。
 */
HttpResponse httpGetPlain(const std::string& host, int port, const std::string& path,
                          int timeoutMs) {
    HttpResponse resp;
    std::string err;
    int fd = connectTcp(host, port, timeoutMs, err);
    if (fd < 0) {
        resp.error = err;
        return resp;
    }

    std::string req = "GET " + path + " HTTP/1.1\r\n";
    req += "Host: " + host + "\r\n";
    req += "Connection: close\r\n\r\n";

    // 循环写出请求（MSG_NOSIGNAL 避免 SIGPIPE）
    size_t sent = 0;
    while (sent < req.size()) {
        ssize_t w = send(fd, req.data() + sent, req.size() - sent, MSG_NOSIGNAL);
        if (w <= 0) {
            resp.error = "send failed";
            close(fd);
            return resp;
        }
        sent += static_cast<size_t>(w);
    }

    // 循环读到对端关闭
    std::string raw;
    char buf[16384];
    while (true) {
        ssize_t r = recv(fd, buf, sizeof(buf), 0);
        if (r <= 0) break;
        raw.append(buf, static_cast<size_t>(r));
    }
    close(fd);

    // 解析状态行与 body
    size_t lineEnd = raw.find("\r\n");
    if (lineEnd == std::string::npos) {
        resp.error = "malformed http response";
        return resp;
    }
    if (sscanf(raw.c_str(), "HTTP/%*s %d", &resp.status) != 1) {
        resp.error = "malformed status line";
        return resp;
    }
    size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        resp.error = "malformed http headers";
        return resp;
    }
    resp.body = raw.substr(headerEnd + 4);
    return resp;
}

/**
 * @brief 解析 URL 为 scheme/host/port/path 四要素
 * @param url 输入 URL
 * @param scheme 输出：协议（仅接受 "http"/"https"）
 * @param host 输出：主机名
 * @param port 输出：端口（默认 http=80 / https=443，可被显式端口覆盖）
 * @param path 输出：路径（含查询串；无路径时为 "/"）
 * @return 解析成功且 host 非空返回 true
 *
 * 实现思路（伪代码）：
 *   找 "://" 切出 scheme；非 http/https 直接失败
 *   按 scheme 赋默认端口（443/80）
 *   authority = "://" 之后到第一个 '/' 之前的部分（无 '/' 则到串尾）
 *   在 authority 中 rfind(':')：
 *       存在且不含 ']'（排除 IPv6 字面量 [::1] 的冒号误判）
 *           -> host=冒号前，port=atoi(冒号后)，port<=0 视为非法
 *       否则 -> host=整个 authority
 *   path = 第一个 '/' 起的内容，无 '/' 则为 "/"
 *   最终要求 host 非空
 */
bool parseUrl(const std::string& url, std::string& scheme, std::string& host, int& port,
              std::string& path) {
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return false;
    scheme = url.substr(0, schemeEnd);
    if (scheme != "http" && scheme != "https") return false;
    port = scheme == "https" ? 443 : 80;  // 按协议赋默认端口

    size_t hostStart = schemeEnd + 3;
    size_t pathStart = url.find('/', hostStart);
    std::string authority =
        pathStart == std::string::npos ? url.substr(hostStart) : url.substr(hostStart, pathStart - hostStart);
    // 显式端口判定：authority 尾部冒号；含 ']' 说明是 IPv6 字面量，不做端口拆分
    size_t colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(']') == std::string::npos) {
        host = authority.substr(0, colon);
        port = atoi(authority.substr(colon + 1).c_str());
        if (port <= 0) return false;
    } else {
        host = authority;
    }
    path = pathStart == std::string::npos ? "/" : url.substr(pathStart);
    return !host.empty();
}

}  // namespace

}  // namespace net

/**
 * @brief 通用 URL 拉取入口（OpenSSL 编译分支）：http 明文 / https TLS 分流
 * @param url 完整 URL
 * @param timeoutMs 超时（毫秒）
 * @return 响应；URL 非法或请求失败时 error 非空
 *
 * 实现思路：
 *   parseUrl 解析 -> scheme=="http" 走 httpGetPlain（明文 socket）；
 *   否则（https）构造 HttpsClient 走 OpenSSL TLS 通道。
 */
HttpResponse fetchUrl(const std::string& url, int timeoutMs) {
    HttpResponse resp;
    std::string scheme, host, path;
    int port = 0;
    if (!parseUrl(url, scheme, host, port, path)) {
        resp.error = "invalid url: " + url;
        return resp;
    }
    if (scheme == "http") return httpGetPlain(host, port, path, timeoutMs);
    HttpsClient client(timeoutMs);
    return client.get(host, port, path);
}

}  // namespace net

#else  // !MCP_WITH_OPENSSL

/*
 * ============================================================================
 * 无 OpenSSL 编译分支（降级实现）
 *
 * 约束：没有 TLS 能力，因此——
 *   1. httpsClientAvailable() 返回 false；
 *   2. HttpsClient::post/get 不发起网络请求，直接返回带提示的错误响应；
 *   3. fetchUrl 仅支持 http://（走明文 httpGetPlain），https:// 返回
 *      "requires MCP_WITH_OPENSSL=ON build" 错误。
 * connectTcp / parseUrl / httpGetPlain 与上方 OpenSSL 分支保持一致。
 * ============================================================================
 */

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace net {

/** @brief 本编译分支无 OpenSSL，HTTPS 客户端不可用 */
bool httpsClientAvailable() { return false; }

namespace {

/**
 * @brief 建立带超时的 TCP 连接（与 OpenSSL 分支中的实现一致）
 * @param host 主机名或 IP
 * @param port 端口
 * @param timeoutMs 超时（毫秒）
 * @param err 输出：失败原因
 * @return 成功返回 fd，失败返回 -1
 *
 * 实现思路：getaddrinfo 解析 -> 遍历候选地址 socket/设超时/connect，
 * 全部失败则填 err 返回 -1。
 */
int connectTcp(const std::string& host, int port, int timeoutMs, std::string& err) {
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        err = "getaddrinfo failed for " + host;
        return -1;
    }

    int fd = -1;
    for (struct addrinfo* p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;

        struct timeval tv;
        tv.tv_sec = timeoutMs / 1000;
        tv.tv_usec = (timeoutMs % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) err = "connect failed to " + host + ":" + portStr;
    return fd;
}

}  // namespace

/**
 * @brief 降级实现：无 OpenSSL，POST 直接返回错误（不发起网络请求）
 * @return error 为 "https client not compiled in" 的响应
 */
HttpResponse HttpsClient::post(const std::string&, int, const std::string&,
                               const std::map<std::string, std::string>&, const std::string&) {
    HttpResponse resp;
    resp.error = "https client not compiled in (rebuild with MCP_WITH_OPENSSL=ON)";
    return resp;
}

/**
 * @brief 降级实现：无 OpenSSL，GET 直接返回错误（不发起网络请求）
 * @return error 为 "https client not compiled in" 的响应
 */
HttpResponse HttpsClient::get(const std::string&, int, const std::string&) {
    HttpResponse resp;
    resp.error = "https client not compiled in (rebuild with MCP_WITH_OPENSSL=ON)";
    return resp;
}

namespace {

/**
 * @brief 解析 URL 为 scheme/host/port/path 四要素（与 OpenSSL 分支实现一致）
 *
 * 实现思路：
 *   "://" 切 scheme（仅 http/https）-> 默认端口 80/443 ->
 *   取 authority（到第一个 '/' 为止）-> rfind(':') 且不含 ']'（排除 IPv6
 *   字面量）则拆出显式端口 -> path 为 '/' 起内容或 "/" -> 要求 host 非空。
 */
bool parseUrl(const std::string& url, std::string& scheme, std::string& host, int& port,
              std::string& path) {
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return false;
    scheme = url.substr(0, schemeEnd);
    if (scheme != "http" && scheme != "https") return false;
    port = scheme == "https" ? 443 : 80;

    size_t hostStart = schemeEnd + 3;
    size_t pathStart = url.find('/', hostStart);
    std::string authority =
        pathStart == std::string::npos ? url.substr(hostStart)
                                       : url.substr(hostStart, pathStart - hostStart);
    size_t colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(']') == std::string::npos) {
        host = authority.substr(0, colon);
        port = atoi(authority.substr(colon + 1).c_str());
        if (port <= 0) return false;
    } else {
        host = authority;
    }
    path = pathStart == std::string::npos ? "/" : url.substr(pathStart);
    return !host.empty();
}

/**
 * @brief 明文 HTTP GET（与 OpenSSL 分支实现一致）：降级分支中 http:// 的唯一通道
 *
 * 实现思路：connectTcp 建连 -> 拼 GET 请求（Connection: close）->
 * 循环 send -> 循环 recv 到对端关闭 -> 解析状态行与 body。
 */
HttpResponse httpGetPlain(const std::string& host, int port, const std::string& path,
                          int timeoutMs) {
    HttpResponse resp;
    std::string err;
    int fd = connectTcp(host, port, timeoutMs, err);
    if (fd < 0) {
        resp.error = err;
        return resp;
    }
    std::string req = "GET " + path + " HTTP/1.1\r\n";
    req += "Host: " + host + "\r\n";
    req += "Connection: close\r\n\r\n";
    size_t sent = 0;
    while (sent < req.size()) {
        ssize_t w = send(fd, req.data() + sent, req.size() - sent, MSG_NOSIGNAL);
        if (w <= 0) {
            resp.error = "send failed";
            close(fd);
            return resp;
        }
        sent += static_cast<size_t>(w);
    }
    std::string raw;
    char buf[16384];
    while (true) {
        ssize_t r = recv(fd, buf, sizeof(buf), 0);
        if (r <= 0) break;
        raw.append(buf, static_cast<size_t>(r));
    }
    close(fd);
    size_t lineEnd = raw.find("\r\n");
    if (lineEnd == std::string::npos) {
        resp.error = "malformed http response";
        return resp;
    }
    if (sscanf(raw.c_str(), "HTTP/%*s %d", &resp.status) != 1) {
        resp.error = "malformed status line";
        return resp;
    }
    size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        resp.error = "malformed http headers";
        return resp;
    }
    resp.body = raw.substr(headerEnd + 4);
    return resp;
}

}  // namespace

/**
 * @brief 通用 URL 拉取入口（降级分支）：仅支持 http:// 明文
 *
 * 实现思路：
 *   parseUrl 解析 -> scheme=="http" 走 httpGetPlain；
 *   scheme=="https" 时无 TLS 能力，返回明确的构建提示错误（降级逻辑）。
 */
HttpResponse fetchUrl(const std::string& url, int timeoutMs) {
    HttpResponse resp;
    std::string scheme, host, path;
    int port = 0;
    if (!parseUrl(url, scheme, host, port, path)) {
        resp.error = "invalid url: " + url;
        return resp;
    }
    if (scheme == "http") return httpGetPlain(host, port, path, timeoutMs);
    // 降级：https 需要 MCP_WITH_OPENSSL=ON 重新构建
    resp.error = "https fetch requires MCP_WITH_OPENSSL=ON build";
    return resp;
}

}  // namespace net

#endif
