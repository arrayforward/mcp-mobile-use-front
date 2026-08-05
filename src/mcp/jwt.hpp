/**
 * @file jwt.hpp
 * @brief JWT 验签器（mcp::JwtVerifier）——支持 HS256 与 RS256
 *
 * 功能：
 *   校验 JWT（RFC 7519）签名与 exp/nbf 时间声明。
 *   - HS256：共享密钥，手写 SHA256/HMAC + 常数时间比较（防时序侧信道）。
 *   - RS256：优先使用 OpenSSL（MCP_WITH_OPENSSL 宏开启），否则回落到
 *     纯 C++ 实现（util::bignum + util::rsa_verify 的 PKCS#1 v1.5 验签，
 *     模数 n 与指数 e 以大端字节存于 m_rs256N/m_rs256E）。
 *   - 密钥来源：本地 PEM 文件 / 内联 PEM 字符串 / JWKS JSON。
 *
 * 开发思路：
 *   1. 为"零依赖可构建"，RS256 提供双路径：OpenSSL 存在时用 EVP 验签；
 *      否则用手写大数模幂实现 RSA-PKCS#1v1.5-SHA256，功能等价但较慢。
 *   2. parseJwks 为静态纯解析方法，不依赖 OpenSSL，便于外部（如
 *      配置加载逻辑）先提取 n/e 再决定加载路径。
 *   3. m_lastError 记录最近一次失败原因，便于排查鉴权 401 的根因；
 *      用 mutable 修饰以便 const verify() 内更新。
 *
 * @author hubin
 * @date 2026-08-05
 */
#pragma once

#include <string>

namespace mcp {

/**
 * @class JwtVerifier
 * @brief JWT 验签器：HS256（共享密钥）与 RS256（公钥/JWKS）
 *
 * 开发思路：
 *   配置与验签分离：setHs256Secret / loadRs256* 系列方法负责加载密钥材料并
 *   置位 m_hs256/m_rs256 开关；verify() 只读这些材料执行验签。
 *   HS256 路径全部手写（sha256+hmac），RS256 路径按编译期宏 MCP_WITH_OPENSSL
 *   二选一，接口对调用方透明。
 *
 * @author hubin
 * @date 2026-08-05
 */
class JwtVerifier {
public:
    /**
     * @brief 设置 HS256 共享密钥
     * @param secret 密钥字符串；空字符串则关闭 HS256
     */
    void setHs256Secret(const std::string& secret);

    /**
     * @brief 从文件加载 PEM 公钥 / x509 证书（兼容保留接口）
     * @param path 文件路径
     * @return 加载成功返回 true，失败原因见 lastError()
     */
    bool loadRs256KeyFile(const std::string& path);
    /**
     * @brief 从字符串加载 PEM 公钥（BEGIN PUBLIC KEY）或 x509 证书
     * @param pem PEM 文本（无 OpenSSL 时仅支持 SPKI 公钥）
     * @return 加载成功返回 true
     */
    bool loadRs256Pem(const std::string& pem);
    /**
     * @brief 从 JWKS（RFC 7517）JSON 加载首个 RSA/RS256 公钥
     * @param jwksJson JWKS 文档字符串
     * @return 加载成功返回 true
     */
    bool loadRs256Jwks(const std::string& jwksJson);

    /** @brief 是否已配置任一验签方式 */
    bool enabled() const { return m_hs256 || m_rs256; }
    /**
     * @brief 校验 JWT：格式 -> base64url 解码 -> 验签 -> exp/nbf 时间声明
     * @param token 三段式 JWT（header.payload.signature）
     * @return 全部通过返回 true；失败原因见 lastError()
     */
    bool verify(const std::string& token) const;
    /** @brief 最近一次 verify/load 失败的原因 */
    const std::string& lastError() const { return m_lastError; }

    /** @brief 当前构建是否支持 RS256（纯 C++ 路径恒可用，故恒为 true） */
    static bool rs256Supported();

    /**
     * @brief 纯解析（不依赖 OpenSSL）：从 JWKS JSON 提取首个
     *        kty=RSA 且 alg 为空或 RS256 的 key 的 n/e（base64url）
     * @param jwksJson JWKS 文档
     * @param nB64Url  [out] 模数 n（base64url）
     * @param eB64Url  [out] 指数 e（base64url）
     * @param err      [out] 失败原因
     * @return 找到并提取成功返回 true
     */
    static bool parseJwks(const std::string& jwksJson, std::string& nB64Url,
                          std::string& eB64Url, std::string& err);

private:
    /**
     * @brief 按 alg 验签：HS256 用 HMAC + 常数时间比较；RS256 走 OpenSSL 或纯 C++ 路径
     * @param alg          header 中的 alg 字段
     * @param signingInput 待验签文本（header.payload 原文）
     * @param signature    解码后的签名字节
     * @return 验签通过返回 true
     */
    bool verifySignature(const std::string& alg, const std::string& signingInput,
                         const std::string& signature) const;
    /**
     * @brief 校验 payload 中的 exp（过期）与 nbf（生效时间）声明
     * @param payloadJson payload 段解码后的 JSON
     * @return 时间声明合法返回 true
     */
    bool checkClaims(const std::string& payloadJson) const;

    bool m_hs256 = false;
    bool m_rs256 = false;
    std::string m_secret;
    std::string m_publicKeyPem;   // OpenSSL 路径使用
    std::string m_rs256N;         // 纯 C++ 路径：n/e 大端字节
    std::string m_rs256E;
    mutable std::string m_lastError;
};

}  // namespace mcp
