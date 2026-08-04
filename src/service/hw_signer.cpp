#include "hw_signer.hpp"

#include <time.h>

#include <cstdio>

#ifdef MCP_WITH_OPENSSL

#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace service {

namespace {

std::string hexEncode(const unsigned char* data, unsigned int len) {
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (unsigned int i = 0; i < len; ++i) {
        out += kHex[data[i] >> 4];
        out += kHex[data[i] & 0xF];
    }
    return out;
}

std::string sha256Hex(const std::string& input) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, input.data(), input.size());
    EVP_DigestFinal_ex(ctx, digest, &len);
    EVP_MD_CTX_free(ctx);
    return hexEncode(digest, len);
}

std::string hmacSha256Hex(const std::string& key, const std::string& input) {
    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(input.data()), input.size(), mac, &len);
    return hexEncode(mac, len);
}

}  // namespace

bool hwSignerAvailable() { return true; }

std::string hwSdkDateNow() {
    char buf[32];
    time_t now = time(nullptr);
    struct tm tmUtc;
    gmtime_r(&now, &tmUtc);
    strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", &tmUtc);
    return buf;
}

std::string hwSignAuthorization(const std::string& ak, const std::string& sk,
                                const std::string& method, const std::string& host,
                                const std::string& uri, const std::string& body,
                                const std::string& sdkDate) {
    std::string canonicalHeaders = "host:" + host + "\n" + "x-sdk-date:" + sdkDate + "\n";
    std::string signedHeaders = "host;x-sdk-date";

    std::string canonicalRequest = method + "\n" + uri + "\n" + "\n" + canonicalHeaders + "\n" +
                                   signedHeaders + "\n" + sha256Hex(body);

    std::string stringToSign =
        "SDK-HMAC-SHA256\n" + sdkDate + "\n" + sha256Hex(canonicalRequest);

    std::string signature = hmacSha256Hex(sk, stringToSign);

    return "SDK-HMAC-SHA256 Access=" + ak + ", SignedHeaders=" + signedHeaders +
           ", Signature=" + signature;
}

}  // namespace service

#else  // !MCP_WITH_OPENSSL

namespace service {

bool hwSignerAvailable() { return false; }

std::string hwSdkDateNow() { return ""; }

std::string hwSignAuthorization(const std::string&, const std::string&, const std::string&,
                                const std::string&, const std::string&, const std::string&,
                                const std::string&) {
    return "";
}

}  // namespace service

#endif
