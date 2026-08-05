/**
 * @file auth.cpp
 * @brief HTTP 鉴权检查器实现——环境变量配置加载与 Authorization 头校验
 *
 * 功能：
 *   实现 auth.hpp 声明的 AuthChecker：
 *   - fromEnv()：读 MCP_JWT_SECRET / MCP_JWT_PUBLIC_KEY / MCP_AUTH_TOKEN；
 *   - RS256 密钥源支持三种形态：本地 PEM 文件路径、内联 PEM 文本、
 *     http(s):// URL（拉取后按内容自动识别 PEM 或 JWKS）；
 *   - check()：按模式校验 Authorization 头（静态 token 或 Bearer JWT）。
 *
 * 开发思路：
 *   1. 匿名命名空间内的 env/loadRs256Source/extractBearer 为纯辅助函数，
 *      不暴露到头文件，保持对外接口最小。
 *   2. JWT 配置失败（如 URL 拉取失败）时回落 None 而非启动失败，
 *      由调用方决定是否视为致命错误。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "auth.hpp"

#include <cstdlib>

#include "../net/https_client.hpp"

namespace mcp {

namespace {

/**
 * @brief 读环境变量，未设置返回空串
 * @param name 变量名
 */
std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

/**
 * @brief 按来源加载 RS256 密钥材料到 verifier
 * @param v      JwtVerifier
 * @param source 本地 PEM 路径 / 内联 PEM / http(s):// URL
 * @return 加载成功返回 true
 *
 * 伪代码：
 *   若 source 以 http(s):// 开头 -> fetchUrl 拉取内容（30s 超时）
 *   若内容含 "-----BEGIN" -> 按 PEM 加载；否则按 JWKS JSON 加载
 */
bool loadRs256Source(JwtVerifier& v, const std::string& source) {
    std::string material = source;
    if (source.compare(0, 7, "http://") == 0 || source.compare(0, 8, "https://") == 0) {
        net::HttpResponse resp = net::fetchUrl(source, 30000);
        if (!resp.ok()) return false;
        material = resp.body;
    }
    if (material.find("-----BEGIN") != std::string::npos) return v.loadRs256Pem(material);
    return v.loadRs256Jwks(material);
}

/**
 * @brief 从请求头提取 Bearer token（无 Bearer 前缀时取整个 Authorization 值）
 * @param req HTTP 请求
 * @param out [out] 提取到的 token
 * @return 提取到非空 token 返回 true
 */
bool extractBearer(const net::Request& req, std::string& out) {
    std::string auth = req.header("Authorization");
    const std::string bearer = "Bearer ";
    if (auth.compare(0, bearer.size(), bearer) == 0) {
        out = auth.substr(bearer.size());
        return true;
    }
    out = auth;
    return !out.empty();
}

}  // namespace

AuthChecker AuthChecker::withToken(const std::string& token) {
    AuthChecker c;
    if (!token.empty()) {
        c.m_mode = Mode::Token;
        c.m_token = token;
    }
    return c;
}

AuthChecker AuthChecker::withJwt(const JwtVerifier& verifier) {
    AuthChecker c;
    if (verifier.enabled()) {
        c.m_mode = Mode::Jwt;
        c.m_jwt = verifier;
    }
    return c;
}

AuthChecker AuthChecker::fromEnv() {
    std::string jwtSecret = env("MCP_JWT_SECRET");
    std::string jwtKeyFile = env("MCP_JWT_PUBLIC_KEY");
    if (!jwtSecret.empty() || !jwtKeyFile.empty()) {
        JwtVerifier v;
        // HS256 共享密钥直接设置；RS256 密钥源加载失败则放弃 JWT 模式
        if (!jwtSecret.empty()) v.setHs256Secret(jwtSecret);
        if (!jwtKeyFile.empty() && !loadRs256Source(v, jwtKeyFile)) return AuthChecker();
        AuthChecker c = withJwt(v);
        if (c.enabled()) return c;
    }
    return withToken(env("MCP_AUTH_TOKEN"));
}

bool AuthChecker::check(const net::Request& req) const {
    switch (m_mode) {
        case Mode::None:
            return true;
        case Mode::Token: {
            // 接受裸 token 或 "Bearer <token>" 两种写法
            std::string auth = req.header("Authorization");
            if (auth == m_token) return true;
            const std::string bearer = "Bearer ";
            return auth.compare(0, bearer.size(), bearer) == 0 &&
                   auth.substr(bearer.size()) == m_token;
        }
        case Mode::Jwt: {
            // 提取 Bearer token 后交给 JwtVerifier 验签与时间校验
            std::string token;
            if (!extractBearer(req, token)) return false;
            return m_jwt.verify(token);
        }
    }
    return false;
}

std::string AuthChecker::describe() const {
    switch (m_mode) {
        case Mode::None: return "none";
        case Mode::Token: return "token";
        case Mode::Jwt: return "jwt";
    }
    return "none";
}

}  // namespace mcp
