/**
 * @file hw_signer.cpp
 * @brief 华为云 SDK-HMAC-SHA256 签名实现（OpenSSL 宏保护，可无依赖降级编译）
 *
 * 功能：
 *   按华为云 API 网关签名规范计算请求签名：
 *   CanonicalRequest -> StringToSign -> HMAC-SHA256(sk) -> Authorization 头。
 *
 * 开发思路：
 *   1. 整个实现包在 #ifdef MCP_WITH_OPENSSL 内：定义该宏时包含
 *      <openssl/evp.h>/<openssl/hmac.h> 走真实 HMAC/SHA256；
 *   2. 未定义时提供同名空实现（返回空串、hwSignerAvailable=false），
 *      保证无 OpenSSL 环境也能编译，上层可降级为 token 鉴权；
 *   3. 签名只覆盖 host 与 x-sdk-date 两个头（SignedHeaders=host;x-sdk-date），
 *      query 串为空（本接口无查询参数），body 参与 sha256 摘要。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "hw_signer.hpp"

#include <time.h>

#include <cstdio>

#ifdef MCP_WITH_OPENSSL

#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace service {

namespace {

/**
 * @brief 字节数组转小写十六进制字符串
 * @param data 字节指针 @param len 字节数
 * @return 2*len 长的 hex 字符串
 * 伪代码：逐字节取高 4 位/低 4 位索引 hex 表追加
 */
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

/**
 * @brief 计算字符串的 SHA256 摘要（hex 编码）
 * @param input 输入原文
 * @return 64 字符的小写 hex 摘要
 * 伪代码：EVP_MD_CTX new/init(sha256)/update/final/free -> hexEncode
 */
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

/**
 * @brief 计算 HMAC-SHA256（hex 编码）
 * @param key 密钥（SK） @param input 待签名串
 * @return 64 字符的小写 hex 签名
 * 伪代码：OpenSSL HMAC(EVP_sha256, key, input) -> hexEncode
 */
std::string hmacSha256Hex(const std::string& key, const std::string& input) {
    unsigned char mac[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(input.data()), input.size(), mac, &len);
    return hexEncode(mac, len);
}

}  // namespace

/** @brief OpenSSL 可用：签名功能就绪 */
bool hwSignerAvailable() { return true; }

/**
 * @brief 生成 X-Sdk-Date 时间戳
 * @return UTC 时间串，格式 "%Y%m%dT%H%M%SZ"（如 20260805T123000Z）
 * 伪代码：time -> gmtime_r -> strftime
 */
std::string hwSdkDateNow() {
    char buf[32];
    time_t now = time(nullptr);
    struct tm tmUtc;
    gmtime_r(&now, &tmUtc);
    strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", &tmUtc);
    return buf;
}

/**
 * @brief 计算 SDK-HMAC-SHA256 的 Authorization 头
 * @param ak/sk 访问密钥对 @param method HTTP 方法 @param host 域名
 * @param uri 请求路径 @param body 请求体 @param sdkDate 时间戳
 * @return 完整 Authorization 头值
 *
 * 实现思路（华为云签名规范三步）：
 *   1. CanonicalRequest = method \n uri \n (空 query) \n
 *      canonicalHeaders \n signedHeaders \n sha256(body)；
 *   2. StringToSign = "SDK-HMAC-SHA256" \n sdkDate \n sha256(CanonicalRequest)；
 *   3. Signature = HMAC-SHA256(sk, StringToSign)，拼入最终头。
 * 伪代码：拼 canonicalRequest -> sha256 -> 拼 stringToSign -> hmac -> 拼头部
 */
std::string hwSignAuthorization(const std::string& ak, const std::string& sk,
                                const std::string& method, const std::string& host,
                                const std::string& uri, const std::string& body,
                                const std::string& sdkDate) {
    // 参与签名的头固定为 host 与 x-sdk-date（须按字典序、小写键名）
    std::string canonicalHeaders = "host:" + host + "\n" + "x-sdk-date:" + sdkDate + "\n";
    std::string signedHeaders = "host;x-sdk-date";

    // 第一步：构造规范请求（query 为空，故第三行为空行）
    std::string canonicalRequest = method + "\n" + uri + "\n" + "\n" + canonicalHeaders + "\n" +
                                   signedHeaders + "\n" + sha256Hex(body);

    // 第二步：构造待签名串
    std::string stringToSign =
        "SDK-HMAC-SHA256\n" + sdkDate + "\n" + sha256Hex(canonicalRequest);

    // 第三步：用 SK 做 HMAC-SHA256 得到签名
    std::string signature = hmacSha256Hex(sk, stringToSign);

    return "SDK-HMAC-SHA256 Access=" + ak + ", SignedHeaders=" + signedHeaders +
           ", Signature=" + signature;
}

}  // namespace service

#else  // !MCP_WITH_OPENSSL

// 无 OpenSSL 降级实现：同名空函数保证可链接，由上层检测 available() 后降级

namespace service {

/** @brief 未启用 OpenSSL：签名不可用 */
bool hwSignerAvailable() { return false; }

/** @brief 降级实现：返回空时间戳 */
std::string hwSdkDateNow() { return ""; }

/** @brief 降级实现：返回空签名头（上层应改用 token 鉴权） */
std::string hwSignAuthorization(const std::string&, const std::string&, const std::string&,
                                const std::string&, const std::string&, const std::string&,
                                const std::string&) {
    return "";
}

}  // namespace service

#endif
