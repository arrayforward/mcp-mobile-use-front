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
class AuthChecker {
public:
    enum class Mode { None, Token, Jwt };

    static AuthChecker none() { return AuthChecker(); }
    static AuthChecker withToken(const std::string& token);
    static AuthChecker withJwt(const JwtVerifier& verifier);
    static AuthChecker fromEnv();

    Mode mode() const { return mode_; }
    bool enabled() const { return mode_ != Mode::None; }
    bool check(const net::Request& req) const;
    std::string describe() const;

private:
    Mode mode_ = Mode::None;
    std::string token_;
    JwtVerifier jwt_;
};

}  // namespace mcp
