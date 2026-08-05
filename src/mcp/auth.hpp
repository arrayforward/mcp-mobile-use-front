/**
 * @file auth.hpp
 * @brief HTTP 鉴权检查器（mcp::AuthChecker）——None / Token / Jwt 三种模式
 *
 * 功能：
 *   对进入 HTTP transport 的请求做统一鉴权：
 *   - None：不校验（默认，便于本地开发与测试）；
 *   - Token：静态 token 比对，接受 "Authorization: <token>" 或 "Bearer <token>"；
 *   - Jwt：校验 "Authorization: Bearer <jwt>"，支持 HS256（共享密钥）与
 *     RS256（PEM 公钥/证书，或 http(s):// 远端 JWKS）。
 *
 * 开发思路：
 *   1. 工厂方法（none/withToken/withJwt/fromEnv）构造，配置错误时安全回落为
 *      None，避免"配置写错导致服务锁死"。
 *   2. fromEnv() 读取 MCP_AUTH_* / MCP_JWT_* 环境变量，使命令行参数
 *      --auth-jwt-public-key 支持三种来源：本地 PEM 文件、内联 PEM、
 *      http(s):// URL（自动按内容识别 PEM 或 JWKS）。
 *   3. /healthz 健康检查路径在 transport 层放行，不经过本检查器。
 *
 * @author hubin
 * @date 2026-08-05
 */
#pragma once

#include <string>

#include "../net/http_server.hpp"
#include "jwt.hpp"

namespace mcp {

// 鉴权方案（启动时可配置，默认 none 不启用）：
//   none   - 不校验（默认，便于测试）
//   token  - 静态 token：Authorization: <token> 或 Bearer <token>
//   jwt    - JWT：Authorization: Bearer <jwt>，支持 HS256（共享密钥）与
//            RS256（PEM 公钥/证书文件，需 OpenSSL 构建）

/**
 * @class AuthChecker
 * @brief 请求鉴权检查器：按模式校验 Authorization 头
 *
 * 开发思路：
 *   值语义小对象：Mode 枚举 + token 字符串 + JwtVerifier 副本，
 *   可安全拷贝到各路由闭包中；check() 为 const 且无副作用，
 *   多线程并发调用无需加锁。
 *
 * @author hubin
 * @date 2026-08-05
 */
class AuthChecker {
public:
    /** @brief 鉴权模式 */
    enum class Mode { None, Token, Jwt };

    /** @brief 构造不鉴权的检查器 */
    static AuthChecker none() { return AuthChecker(); }
    /**
     * @brief 构造静态 token 模式；token 为空时回落为 None
     * @param token 期望的静态令牌
     */
    static AuthChecker withToken(const std::string& token);
    /**
     * @brief 构造 JWT 模式；verifier 未启用时回落为 None
     * @param verifier 已配置密钥的 JwtVerifier
     */
    static AuthChecker withJwt(const JwtVerifier& verifier);
    /**
     * @brief 从环境变量构造：优先 MCP_JWT_SECRET / MCP_JWT_PUBLIC_KEY，
     *        其次 MCP_AUTH_TOKEN；均缺失时为 None
     */
    static AuthChecker fromEnv();

    /** @brief 当前模式 */
    Mode mode() const { return m_mode; }
    /** @brief 是否启用了鉴权（非 None） */
    bool enabled() const { return m_mode != Mode::None; }
    /**
     * @brief 校验请求是否通过鉴权
     * @param req HTTP 请求（读取其 Authorization 头）
     * @return 通过返回 true；None 模式恒为 true
     */
    bool check(const net::Request& req) const;
    /** @brief 模式名称（"none"/"token"/"jwt"），用于日志 */
    std::string describe() const;

private:
    Mode m_mode = Mode::None;
    std::string m_token;
    JwtVerifier m_jwt;
};

}  // namespace mcp
