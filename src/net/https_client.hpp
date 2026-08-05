/**
 * @file https_client.hpp
 * @brief 简易 HTTPS/HTTP 客户端（net::HttpsClient / fetchUrl）——外联资源拉取组件
 *
 * 功能：
 *   提供面向主机/路径的 HTTPS POST/GET（需 MCP_WITH_OPENSSL 编译），
 *   以及通用入口 fetchUrl：根据 URL scheme 自动分流——http:// 走明文
 *   socket 直连，https:// 走 OpenSSL TLS 加密通道。
 *
 * 开发思路：
 *   1. 为满足"零第三方依赖"约束，不引入 libcurl，直接基于
 *      getaddrinfo + socket + OpenSSL 手写最小可用的 HTTP 客户端。
 *   2. 编译期可裁剪：MCP_WITH_OPENSSL 开启时提供完整 https 能力；
 *      未开启时（见 https_client.cpp 的 #else 分支）HttpsClient 的
 *      post/get 返回带错误信息的降级响应，fetchUrl 仅支持 http://
 *      明文拉取，https:// 请求返回明确错误提示。
 *   3. 响应模型极简：Connection: close 短连接，一次性读完响应体，
 *      按 "\r\n\r\n" 切分头与体，不解析 chunked 等复杂传输编码，
 *      足够覆盖"拉公钥/拉小段 JSON 资源"的使用场景。
 *
 * @author hubin
 * @date 2026-08-05
 */
#pragma once

#include <map>
#include <string>

namespace net {

/**
 * @struct HttpResponse
 * @brief HTTP 客户端响应：状态码、响应头、正文与错误信息
 *
 * 开发思路：
 *   网络/协议错误不抛异常，而是填入 error 字段（status 保持 0），
 *   调用方统一用 ok() 或检查 error 判成败，简化错误处理路径。
 */
struct HttpResponse {
    int status = 0;                                 ///< HTTP 状态码（请求失败时为 0）
    std::map<std::string, std::string> headers;     ///< 响应头（当前实现未填充，保留扩展）
    std::string body;                               ///< 响应正文
    std::string error;                              ///< 错误描述；非空表示请求失败

    /** @brief 是否成功：无错误且状态码为 2xx */
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

/**
 * @class HttpsClient
 * @brief HTTPS 客户端：基于 OpenSSL 的 TLS 加密 POST/GET
 *
 * 开发思路：
 *   post/get 仅是 request() 的薄封装；request() 内完成
 *   TCP 连接（带超时）-> TLS 握手（含 SNI）-> 拼 HTTP/1.1 请求 ->
 *   循环写 -> 一次性读全部响应 -> 解析状态行与 body 的完整流程。
 *   使用 Connection: close 短连接，每次请求独立建连，实现简单可靠。
 */
class HttpsClient {
public:
    /**
     * @brief 构造客户端
     * @param timeoutMs 连接/读写超时（毫秒），默认 30000
     */
    explicit HttpsClient(int timeoutMs = 30000) : m_timeoutMs(timeoutMs) {}

    /**
     * @brief 发送 HTTPS POST 请求
     * @param host 目标主机名
     * @param port 目标端口（通常 443）
     * @param path 请求路径（含查询串）
     * @param headers 附加请求头
     * @param body 请求体
     * @return 响应；失败时 error 非空
     */
    HttpResponse post(const std::string& host, int port, const std::string& path,
                      const std::map<std::string, std::string>& headers,
                      const std::string& body);

    /**
     * @brief 发送 HTTPS GET 请求
     * @param host 目标主机名
     * @param port 目标端口（通常 443）
     * @param path 请求路径（含查询串）
     * @return 响应；失败时 error 非空
     */
    HttpResponse get(const std::string& host, int port, const std::string& path);

private:
    /**
     * @brief 请求主流程：建连 -> TLS 握手 -> 发送 -> 收响应 -> 解析
     * @param method HTTP 方法（"GET"/"POST"）
     * @param host 主机名
     * @param port 端口
     * @param path 路径
     * @param headers 附加请求头
     * @param body 请求体（可为空）
     * @return 响应；任一步失败提前返回且 error 非空
     */
    HttpResponse request(const std::string& method, const std::string& host, int port,
                         const std::string& path,
                         const std::map<std::string, std::string>& headers,
                         const std::string& body);
    int m_timeoutMs;  ///< 超时（毫秒）
};

/**
 * @brief 通用 URL 拉取：支持 http:// 与 https://（https 需要 MCP_WITH_OPENSSL 构建）
 *
 * 实现思路：
 *   先用 parseUrl 解析出 scheme/host/port/path；scheme 为 "http" 时走
 *   明文 socket 的 httpGetPlain，为 "https" 时构造 HttpsClient 走 TLS。
 *   未编译 OpenSSL 时 https:// 返回带提示的错误响应（降级仅支持 http）。
 *
 * @param url 完整 URL，如 "https://example.com:8443/path?a=1"
 * @param timeoutMs 超时（毫秒），默认 30000
 * @return 响应；URL 非法或请求失败时 error 非空
 */
HttpResponse fetchUrl(const std::string& url, int timeoutMs = 30000);

/**
 * @brief 当前构建是否内置 HTTPS 客户端能力
 * @return 编译了 MCP_WITH_OPENSSL 返回 true，否则 false
 */
bool httpsClientAvailable();

}  // namespace net
