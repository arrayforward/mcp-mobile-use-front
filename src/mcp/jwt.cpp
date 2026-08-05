/**
 * @file jwt.cpp
 * @brief JWT 验签实现——HS256（手写 HMAC-SHA256）与 RS256（OpenSSL / 纯 C++ 双路径）
 *
 * 功能：
 *   实现 jwt.hpp 声明的 JwtVerifier：
 *   - 三段式 JWT 解析与 base64url 解码；
 *   - HS256：手写 HMAC-SHA256 计算期望签名，逐字节 OR 累加做常数时间比较，
 *     避免时序攻击泄露签名前缀；
 *   - RS256：MCP_WITH_OPENSSL 宏开启时用 EVP_DigestVerify 验签；
 *     否则用纯 C++（bignum 大数模幂 + rsa_verify）实现 PKCS#1 v1.5-SHA256，
 *     模数/指数存于 m_rs256N/m_rs256E；
 *   - exp/nbf 时间声明校验；
 *   - JWKS（RFC 7517）解析：选第一个 kty=RSA 且 alg 兼容 RS256 的 key。
 *
 * 开发思路：
 *   1. 所有失败路径统一写 m_lastError 并返回 false，调用方只查布尔值，
 *      排障时再读 lastError()。
 *   2. JWKS -> PEM 的转换（OpenSSL 路径）在加载时一次性完成，
 *      verify 时直接用 PEM，避免每次请求重复构造 RSA 对象。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "jwt.hpp"

#include <time.h>

#include <fstream>
#include <sstream>

#include "../json/json.hpp"
#include "../util/rsa_verify.hpp"
#include "../util/sha256.hpp"

#ifdef MCP_WITH_OPENSSL
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#endif

namespace mcp {

void JwtVerifier::setHs256Secret(const std::string& secret) {
    m_secret = secret;
    m_hs256 = !secret.empty();
}

bool JwtVerifier::rs256Supported() { return true; }

bool JwtVerifier::loadRs256KeyFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        m_lastError = "cannot open key file: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return loadRs256Pem(ss.str());
}

bool JwtVerifier::loadRs256Pem(const std::string& pem) {
    if (pem.find("-----BEGIN") == std::string::npos) {
        m_lastError = "key material is not PEM";
        return false;
    }
#ifdef MCP_WITH_OPENSSL
    // OpenSSL 路径：直接保存 PEM 原文，验签时由 EVP 解析（公钥与证书都支持）
    m_publicKeyPem = pem;
#else
    // 纯 C++ 路径：仅支持 SPKI 公钥（BEGIN PUBLIC KEY），x509 证书需要 OpenSSL
    if (pem.find("BEGIN PUBLIC KEY") == std::string::npos) {
        m_lastError = "without OpenSSL only SPKI PEM (BEGIN PUBLIC KEY) is supported, "
                      "x509 certificates require MCP_WITH_OPENSSL=ON";
        return false;
    }
    // 解析 SPKI DER 结构，提取 RSA 模数 n 与指数 e（大端字节）
    if (!util::parseSpkiPem(pem, m_rs256N, m_rs256E)) {
        m_lastError = "failed to parse SPKI public key";
        return false;
    }
#endif
    m_rs256 = true;
    return true;
}

bool JwtVerifier::parseJwks(const std::string& jwksJson, std::string& nB64Url,
                            std::string& eB64Url, std::string& err) {
    mj::Value doc;
    try {
        doc = mj::Value::parse(jwksJson);
    } catch (const std::exception& e) {
        err = std::string("jwks is not valid json: ") + e.what();
        return false;
    }
    const mj::Value& keys = doc["keys"];
    if (!keys.isArray() || keys.size() == 0) {
        err = "jwks contains no keys";
        return false;
    }
    // 顺序扫描，选第一个 kty=RSA 且 alg 为空或 RS256 的 key
    for (const auto& key : keys.asArray()) {
        std::string kty = key["kty"].asString();
        std::string alg = key["alg"].asString();
        if (kty == "RSA" && (alg.empty() || alg == "RS256")) {
            nB64Url = key["n"].asString();
            eB64Url = key["e"].asString();
            if (nB64Url.empty() || eB64Url.empty()) {
                err = "jwks rsa key missing n/e";
                return false;
            }
            return true;
        }
    }
    err = "jwks contains no RSA/RS256 key";
    return false;
}

bool JwtVerifier::loadRs256Jwks(const std::string& jwksJson) {
    // 第一步：从 JWKS 提取 n/e（base64url 文本）
    std::string nB64Url, eB64Url, err;
    if (!parseJwks(jwksJson, nB64Url, eB64Url, err)) {
        m_lastError = err;
        return false;
    }
    // 第二步：解码为大端字节，供纯 C++ 验签路径使用
    if (!util::base64UrlDecode(nB64Url, m_rs256N) || !util::base64UrlDecode(eB64Url, m_rs256E)) {
        m_lastError = "jwks n/e are not valid base64url";
        return false;
    }

#ifdef MCP_WITH_OPENSSL
    // 构造 PEM 供 OpenSSL 路径验签
    // n/e 大端字节 -> BIGNUM -> RSA -> EVP_PKEY -> 写出 SPKI PEM
    BIGNUM* n = BN_bin2bn(reinterpret_cast<const unsigned char*>(m_rs256N.data()),
                          static_cast<int>(m_rs256N.size()), nullptr);
    BIGNUM* e = BN_bin2bn(reinterpret_cast<const unsigned char*>(m_rs256E.data()),
                          static_cast<int>(m_rs256E.size()), nullptr);
    if (!n || !e) {
        if (n) BN_free(n);
        if (e) BN_free(e);
        m_lastError = "failed to decode jwks n/e";
        return false;
    }

    RSA* rsa = RSA_new();
    RSA_set0_key(rsa, n, e, nullptr);
    EVP_PKEY* pkey = EVP_PKEY_new();
    EVP_PKEY_assign_RSA(pkey, rsa);

    BIO* bio = BIO_new(BIO_s_mem());
    if (!PEM_write_bio_PUBKEY(bio, pkey)) {
        BIO_free(bio);
        EVP_PKEY_free(pkey);
        m_lastError = "failed to serialize jwks public key";
        return false;
    }
    char* pemData = nullptr;
    long pemLen = BIO_get_mem_data(bio, &pemData);
    m_publicKeyPem.assign(pemData, static_cast<size_t>(pemLen));
    BIO_free(bio);
    EVP_PKEY_free(pkey);
#endif
    m_rs256 = true;
    return true;
}

bool JwtVerifier::verify(const std::string& token) const {
    m_lastError.clear();

    // 伪代码：split(header.payload.signature) -> 解码三段 -> 验签 -> 时间声明
    size_t dot1 = token.find('.');
    size_t dot2 = token.rfind('.');
    if (dot1 == std::string::npos || dot2 == dot1 || dot2 == std::string::npos) {
        m_lastError = "malformed jwt";
        return false;
    }

    // signingInput 是 "header.payload" 原文（base64url 未解码形态）
    std::string signingInput = token.substr(0, dot2);
    std::string signatureB64 = token.substr(dot2 + 1);

    std::string headerJson;
    if (!util::base64UrlDecode(token.substr(0, dot1), headerJson)) {
        m_lastError = "bad jwt header encoding";
        return false;
    }
    std::string payloadJson;
    if (!util::base64UrlDecode(token.substr(dot1 + 1, dot2 - dot1 - 1), payloadJson)) {
        m_lastError = "bad jwt payload encoding";
        return false;
    }
    std::string signature;
    if (!util::base64UrlDecode(signatureB64, signature)) {
        m_lastError = "bad jwt signature encoding";
        return false;
    }

    std::string alg;
    try {
        alg = mj::Value::parse(headerJson)["alg"].asString();
    } catch (const std::exception&) {
        m_lastError = "bad jwt header json";
        return false;
    }

    if (!verifySignature(alg, signingInput, signature)) return false;
    if (!checkClaims(payloadJson)) return false;
    return true;
}

bool JwtVerifier::verifySignature(const std::string& alg, const std::string& signingInput,
                                  const std::string& signature) const {
    if (alg == "HS256") {
        if (!m_hs256) {
            m_lastError = "HS256 not configured";
            return false;
        }
        // 手写 HMAC-SHA256 计算期望签名
        std::string expected = util::hmacSha256(m_secret, signingInput);
        if (expected.size() != signature.size()) {
            m_lastError = "bad signature";
            return false;
        }
        // 常数时间比较：所有字节异或后 OR 累加，diff!=0 即不等，
        // 比较耗时不随匹配前缀长度变化，防时序侧信道
        unsigned char diff = 0;
        for (size_t i = 0; i < expected.size(); ++i)
            diff |= static_cast<unsigned char>(expected[i]) ^
                    static_cast<unsigned char>(signature[i]);
        if (diff != 0) {
            m_lastError = "bad signature";
            return false;
        }
        return true;
    }

    if (alg == "RS256") {
        if (!m_rs256) {
            m_lastError = "RS256 not configured";
            return false;
        }
#ifdef MCP_WITH_OPENSSL
        if (!m_publicKeyPem.empty()) {
            // OpenSSL 路径：优先按 SPKI 公钥解析，失败再按 x509 证书解析
            BIO* bio = BIO_new_mem_buf(m_publicKeyPem.data(),
                                       static_cast<int>(m_publicKeyPem.size()));
            if (!bio) {
                m_lastError = "out of memory";
                return false;
            }
            EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
            if (!pkey) {
                // 不是公钥则尝试 x509 证书，从证书中提取公钥
                BIO_free(bio);
                bio = BIO_new_mem_buf(m_publicKeyPem.data(),
                                      static_cast<int>(m_publicKeyPem.size()));
                X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
                if (cert) {
                    pkey = X509_get_pubkey(cert);
                    X509_free(cert);
                }
            }
            BIO_free(bio);
            if (!pkey) {
                m_lastError = "cannot parse public key / certificate";
                return false;
            }
            // EVP_DigestVerify：SHA256 + RSA 一步验签
            EVP_MD_CTX* ctx = EVP_MD_CTX_new();
            bool ok = false;
            if (ctx && EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1 &&
                EVP_DigestVerifyUpdate(ctx, signingInput.data(), signingInput.size()) == 1) {
                ok = EVP_DigestVerifyFinal(
                         ctx, reinterpret_cast<const unsigned char*>(signature.data()),
                         signature.size()) == 1;
            }
            if (ctx) EVP_MD_CTX_free(ctx);
            EVP_PKEY_free(pkey);
            if (!ok) m_lastError = "bad signature";
            return ok;
        }
#endif
        // 纯 C++ RSA-PKCS#1v1.5-SHA256 验签
        // 先对 signingInput 算 SHA256，再用 m_rs256N/m_rs256E 做大数模幂验签
        unsigned char digest[32];
        util::sha256(reinterpret_cast<const unsigned char*>(signingInput.data()),
                     signingInput.size(), digest);
        bool ok = util::rsaVerifyPkcs1Sha256(
            m_rs256N, m_rs256E, signature,
            std::string(reinterpret_cast<char*>(digest), 32));
        if (!ok) m_lastError = "bad signature";
        return ok;
    }

    m_lastError = "unsupported alg: " + alg;
    return false;
}

bool JwtVerifier::checkClaims(const std::string& payloadJson) const {
    mj::Value payload;
    try {
        payload = mj::Value::parse(payloadJson);
    } catch (const std::exception&) {
        m_lastError = "bad jwt payload json";
        return false;
    }
    // exp：当前时间必须早于过期时间
    if (payload.has("exp") && payload["exp"].isNumber()) {
        long long exp = payload["exp"].asInt64();
        if (static_cast<long long>(time(nullptr)) >= exp) {
            m_lastError = "jwt expired";
            return false;
        }
    }
    // nbf：当前时间必须不早于生效时间
    if (payload.has("nbf") && payload["nbf"].isNumber()) {
        long long nbf = payload["nbf"].asInt64();
        if (static_cast<long long>(time(nullptr)) < nbf) {
            m_lastError = "jwt not yet valid";
            return false;
        }
    }
    return true;
}

}  // namespace mcp
