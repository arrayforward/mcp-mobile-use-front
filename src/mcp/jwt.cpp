#include "jwt.hpp"

#include <time.h>

#include <fstream>
#include <sstream>

#include "../json/json.hpp"
#include "../util/sha256.hpp"

#ifdef MCP_WITH_OPENSSL
#include <openssl/evp.h>
#include <openssl/pem.h>
#endif

namespace mcp {

void JwtVerifier::setHs256Secret(const std::string& secret) {
    secret_ = secret;
    hs256_ = !secret.empty();
}

bool JwtVerifier::rs256Supported() {
#ifdef MCP_WITH_OPENSSL
    return true;
#else
    return false;
#endif
}

bool JwtVerifier::loadRs256KeyFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        lastError_ = "cannot open key file: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    publicKeyPem_ = ss.str();
    if (publicKeyPem_.find("-----BEGIN") == std::string::npos) {
        lastError_ = "key file is not PEM: " + path;
        return false;
    }
    if (!rs256Supported()) {
        lastError_ = "RS256 requires MCP_WITH_OPENSSL=ON build";
        return false;
    }
    rs256_ = true;
    return true;
}

bool JwtVerifier::verify(const std::string& token) const {
    lastError_.clear();

    size_t dot1 = token.find('.');
    size_t dot2 = token.rfind('.');
    if (dot1 == std::string::npos || dot2 == dot1 || dot2 == std::string::npos) {
        lastError_ = "malformed jwt";
        return false;
    }

    std::string signingInput = token.substr(0, dot2);
    std::string signatureB64 = token.substr(dot2 + 1);

    std::string headerJson;
    if (!util::base64UrlDecode(token.substr(0, dot1), headerJson)) {
        lastError_ = "bad jwt header encoding";
        return false;
    }
    std::string payloadJson;
    if (!util::base64UrlDecode(token.substr(dot1 + 1, dot2 - dot1 - 1), payloadJson)) {
        lastError_ = "bad jwt payload encoding";
        return false;
    }
    std::string signature;
    if (!util::base64UrlDecode(signatureB64, signature)) {
        lastError_ = "bad jwt signature encoding";
        return false;
    }

    std::string alg;
    try {
        alg = mj::Value::parse(headerJson)["alg"].asString();
    } catch (const std::exception&) {
        lastError_ = "bad jwt header json";
        return false;
    }

    if (!verifySignature(alg, signingInput, signature)) return false;
    if (!checkClaims(payloadJson)) return false;
    return true;
}

bool JwtVerifier::verifySignature(const std::string& alg, const std::string& signingInput,
                                  const std::string& signature) const {
    if (alg == "HS256") {
        if (!hs256_) {
            lastError_ = "HS256 not configured";
            return false;
        }
        std::string expected = util::hmacSha256(secret_, signingInput);
        if (expected.size() != signature.size()) {
            lastError_ = "bad signature";
            return false;
        }
        unsigned char diff = 0;
        for (size_t i = 0; i < expected.size(); ++i)
            diff |= static_cast<unsigned char>(expected[i]) ^
                    static_cast<unsigned char>(signature[i]);
        if (diff != 0) {
            lastError_ = "bad signature";
            return false;
        }
        return true;
    }

    if (alg == "RS256") {
        if (!rs256_) {
            lastError_ = "RS256 not configured";
            return false;
        }
#ifdef MCP_WITH_OPENSSL
        BIO* bio = BIO_new_mem_buf(publicKeyPem_.data(),
                                   static_cast<int>(publicKeyPem_.size()));
        if (!bio) {
            lastError_ = "out of memory";
            return false;
        }
        EVP_PKEY* pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
        if (!pkey) {
            BIO_free(bio);
            bio = BIO_new_mem_buf(publicKeyPem_.data(),
                                  static_cast<int>(publicKeyPem_.size()));
            X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
            if (cert) {
                pkey = X509_get_pubkey(cert);
                X509_free(cert);
            }
        }
        BIO_free(bio);
        if (!pkey) {
            lastError_ = "cannot parse public key / certificate";
            return false;
        }
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
        if (!ok) lastError_ = "bad signature";
        return ok;
#else
        lastError_ = "RS256 requires MCP_WITH_OPENSSL=ON build";
        return false;
#endif
    }

    lastError_ = "unsupported alg: " + alg;
    return false;
}

bool JwtVerifier::checkClaims(const std::string& payloadJson) const {
    mj::Value payload;
    try {
        payload = mj::Value::parse(payloadJson);
    } catch (const std::exception&) {
        lastError_ = "bad jwt payload json";
        return false;
    }
    if (payload.has("exp") && payload["exp"].isNumber()) {
        long long exp = payload["exp"].asInt64();
        if (static_cast<long long>(time(nullptr)) >= exp) {
            lastError_ = "jwt expired";
            return false;
        }
    }
    if (payload.has("nbf") && payload["nbf"].isNumber()) {
        long long nbf = payload["nbf"].asInt64();
        if (static_cast<long long>(time(nullptr)) < nbf) {
            lastError_ = "jwt not yet valid";
            return false;
        }
    }
    return true;
}

}  // namespace mcp
