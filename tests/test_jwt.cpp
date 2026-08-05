#include "test_framework.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "mcp/jwt.hpp"
#include "util/sha256.hpp"

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string fixture(const char* name) {
    const char* base = std::getenv("RSA_VECTORS_DIR");
    std::string dir = base ? base : "C:/Users/hubinix/AppData/Local/Temp/opencode/rsa-vectors";
    return readFile(dir + "/" + name);
}

void testJwtRs256() {
    std::string pem = fixture("public.pem");
    std::string jwks = fixture("jwks.json");
    std::string good = fixture("good.jwt");
    std::string expired = fixture("expired.jwt");
    std::string wrongSig = fixture("wrong-sig.jwt");
    std::string tampered = fixture("tampered.jwt");

    // PEM（SPKI）验签
    mcp::JwtVerifier pemVerifier;
    CHECK(pemVerifier.loadRs256Pem(pem));
    CHECK(pemVerifier.verify(good));
    CHECK(!pemVerifier.verify(expired));    // exp 过期
    CHECK(!pemVerifier.verify(wrongSig));   // 错误密钥签名
    CHECK(!pemVerifier.verify(tampered));   // payload 篡改

    // JWKS 验签
    mcp::JwtVerifier jwksVerifier;
    CHECK(jwksVerifier.loadRs256Jwks(jwks));
    CHECK(jwksVerifier.verify(good));
    CHECK(!jwksVerifier.verify(tampered));

    // 未配置 RS256 拒绝
    mcp::JwtVerifier empty;
    CHECK(!empty.verify(good));

    // 非 RSA 公钥 PEM 拒绝
    mcp::JwtVerifier bad;
    CHECK(!bad.loadRs256Pem("-----BEGIN PUBLIC KEY-----\nAAAA\n-----END PUBLIC KEY-----\n"));
}

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

void testJwtJwksParse() {
    // 模拟 mcp-proxy /api/auth/jwks 响应（RS256 + 干扰的 EC key）
    std::string jwks =
        "{\"keys\":[{\"kty\":\"RSA\",\"use\":\"sig\",\"alg\":\"RS256\",\"kid\":"
        "\"mcp-proxy-rs256-1\","
        "\"n\":\"nEPwQVXHcH2ySJKsKQp4pIuFzHtVcXkD9kM8yB6rA7fG0jL5mN2oP1qR3sT4uV6wX7yZ8"
        "aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ0_9-1A2b3C4d5E6f7G8h9I0j1K2l3M4n5O6p7Q8r9S0t1U2v3W4x5Y6z7_8-9A\","
        "\"e\":\"AQAB\"},"
        "{\"kty\":\"EC\",\"alg\":\"ES256\",\"crv\":\"P-256\",\"x\":\"x\",\"y\":\"y\"}]}";

    std::string n, e, err;
    CHECK(mcp::JwtVerifier::parseJwks(jwks, n, e, err));
    CHECK(n ==
          "nEPwQVXHcH2ySJKsKQp4pIuFzHtVcXkD9kM8yB6rA7fG0jL5mN2oP1qR3sT4uV6wX7yZ8"
          "aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ0_9-1A2b3C4d5E6f7G8h9I0j1K2l3M4n5O6p7Q8r9S0t1U2v3W4x5Y6z7_8-9A");
    CHECK(e == "AQAB");
    CHECK(err.empty());

    // 无 RSA key 拒绝
    std::string noRsa = "{\"keys\":[{\"kty\":\"EC\",\"alg\":\"ES256\",\"crv\":\"P-256\"}]}";
    CHECK(!mcp::JwtVerifier::parseJwks(noRsa, n, e, err));
    CHECK(!err.empty());

    // 空 keys 拒绝
    CHECK(!mcp::JwtVerifier::parseJwks("{\"keys\":[]}", n, e, err));

    // 非法 JSON 拒绝
    CHECK(!mcp::JwtVerifier::parseJwks("{bad", n, e, err));
}

}  // namespace

void runJwtTests() {
    testJwtHs256();
    testJwtRs256();
    testJwtJwksParse();
}
