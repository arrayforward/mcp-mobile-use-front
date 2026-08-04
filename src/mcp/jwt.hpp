#pragma once

#include <string>

namespace mcp {

class JwtVerifier {
public:
    void setHs256Secret(const std::string& secret);
    bool loadRs256KeyFile(const std::string& path);

    bool enabled() const { return hs256_ || rs256_; }
    bool verify(const std::string& token) const;
    const std::string& lastError() const { return lastError_; }

    static bool rs256Supported();

private:
    bool verifySignature(const std::string& alg, const std::string& signingInput,
                         const std::string& signature) const;
    bool checkClaims(const std::string& payloadJson) const;

    bool hs256_ = false;
    bool rs256_ = false;
    std::string secret_;
    std::string publicKeyPem_;
    mutable std::string lastError_;
};

}  // namespace mcp
