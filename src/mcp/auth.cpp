#include "auth.hpp"

#include <cstdlib>

namespace mcp {

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

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
        c.mode_ = Mode::Token;
        c.token_ = token;
    }
    return c;
}

AuthChecker AuthChecker::withJwt(const JwtVerifier& verifier) {
    AuthChecker c;
    if (verifier.enabled()) {
        c.mode_ = Mode::Jwt;
        c.jwt_ = verifier;
    }
    return c;
}

AuthChecker AuthChecker::fromEnv() {
    std::string jwtSecret = env("MCP_JWT_SECRET");
    std::string jwtKeyFile = env("MCP_JWT_PUBLIC_KEY");
    if (!jwtSecret.empty() || !jwtKeyFile.empty()) {
        JwtVerifier v;
        if (!jwtSecret.empty()) v.setHs256Secret(jwtSecret);
        if (!jwtKeyFile.empty()) v.loadRs256KeyFile(jwtKeyFile);
        AuthChecker c = withJwt(v);
        if (c.enabled()) return c;
    }
    return withToken(env("MCP_AUTH_TOKEN"));
}

bool AuthChecker::check(const net::Request& req) const {
    switch (mode_) {
        case Mode::None:
            return true;
        case Mode::Token: {
            std::string auth = req.header("Authorization");
            if (auth == token_) return true;
            const std::string bearer = "Bearer ";
            return auth.compare(0, bearer.size(), bearer) == 0 &&
                   auth.substr(bearer.size()) == token_;
        }
        case Mode::Jwt: {
            std::string token;
            if (!extractBearer(req, token)) return false;
            return jwt_.verify(token);
        }
    }
    return false;
}

std::string AuthChecker::describe() const {
    switch (mode_) {
        case Mode::None: return "none";
        case Mode::Token: return "token";
        case Mode::Jwt: return "jwt";
    }
    return "none";
}

}  // namespace mcp
