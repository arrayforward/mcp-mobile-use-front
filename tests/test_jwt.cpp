#include "test_framework.hpp"

#include "mcp/jwt.hpp"
#include "util/sha256.hpp"

namespace {

void testJwtHs256() {
    // 手工构造 HS256 token（RFC 风格）
    std::string header = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    std::string payload = "{\"sub\":\"e2e\",\"exp\":4102444800}";
    auto b64url = [](const std::string& s) {
        return util::base64UrlEncode(reinterpret_cast<const unsigned char*>(s.data()),
                                     s.size());
    };
    std::string signingInput = b64url(header) + "." + b64url(payload);
    std::string secret = "test-secret";
    std::string sig = util::base64UrlEncode(
        reinterpret_cast<const unsigned char*>(util::hmacSha256(secret, signingInput).data()),
        32);

    mcp::JwtVerifier v;
    v.setHs256Secret(secret);
    CHECK(v.enabled());
    CHECK(v.verify(signingInput + "." + sig));

    // 错误密钥拒绝
    mcp::JwtVerifier wrong;
    wrong.setHs256Secret("other-secret");
    CHECK(!wrong.verify(signingInput + "." + sig));

    // 篡改 payload 拒绝
    std::string tampered = signingInput + "X." + sig;
    CHECK(!v.verify(tampered));

    // 过期 token（exp=1）拒绝
    std::string expiredPayload = "{\"sub\":\"e2e\",\"exp\":1}";
    std::string expInput = b64url(header) + "." + b64url(expiredPayload);
    std::string expSig = util::base64UrlEncode(
        reinterpret_cast<const unsigned char*>(util::hmacSha256(secret, expInput).data()), 32);
    CHECK(!v.verify(expInput + "." + expSig));

    // 畸形 token 拒绝
    CHECK(!v.verify("not-a-jwt"));
}

}  // namespace

void runJwtTests() {
    testJwtHs256();
}
