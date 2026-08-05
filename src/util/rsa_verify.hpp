/**
 * @file rsa_verify.hpp
 * @brief RSA PKCS#1 v1.5（SHA-256）验签与 SPKI 公钥解析——纯 C++ 零依赖实现
 *
 * 功能：
 *   1. rsaVerifyPkcs1Sha256：给定 RSA 公钥 (n, e) 与 SHA-256 摘要，
 *      验证 PKCS#1 v1.5 签名（JWT RS256 验签的核心）。
 *   2. parseSpkiPem：解析 PEM 编码的 SubjectPublicKeyInfo
 *      （-----BEGIN PUBLIC KEY-----），提取 RSA 模数 n 与指数 e。
 *
 * 开发思路：
 *   1. 为满足"零依赖全手写"约束（不引入 OpenSSL），验签基于自研
 *      bignum（bigModPow）与 sha256 模块组合实现。
 *   2. SPKI 解析手写最简 DER TLV 读取器，按
 *      SEQUENCE { AlgorithmIdentifier, BIT STRING(RSAPublicKey) } 结构
 *      逐层下钻，不校验 OID 内容（只支持 RSA 场景）。
 *   3. 安全要点：EM 的 DigestInfo 比较必须常数时间（异或累积 diff），
 *      防止时序侧信道逐字节探测合法填充。
 *
 * @author hubin
 * @date 2026-08-05
 */
#pragma once

#include <string>

namespace util {

// RSA PKCS#1 v1.5（SHA-256）验签，纯 C++ 实现（无 OpenSSL 依赖）
// n/e：大端字节；signature：大端字节（与 n 等长）；digest：32 字节 SHA-256
/**
 * @brief RSA PKCS#1 v1.5（SHA-256）验签
 * @param nBytes RSA 模数 n（大端字节）
 * @param eBytes RSA 公钥指数 e（大端字节）
 * @param signature 签名（大端字节，与 n 等长）
 * @param digest 消息摘要（32 字节 SHA-256）
 * @return 验签通过返回 true
 */
bool rsaVerifyPkcs1Sha256(const std::string& nBytes, const std::string& eBytes,
                          const std::string& signature, const std::string& digest);

// 解析 SPKI PEM（-----BEGIN PUBLIC KEY-----）提取 RSA n/e（大端字节）
/**
 * @brief 解析 SPKI PEM 公钥
 * @param pem PEM 文本（-----BEGIN PUBLIC KEY----- 包裹的 Base64）
 * @param nBytes 输出：RSA 模数 n（大端字节）
 * @param eBytes 输出：RSA 公钥指数 e（大端字节）
 * @return 解析成功返回 true
 */
bool parseSpkiPem(const std::string& pem, std::string& nBytes, std::string& eBytes);

}  // namespace util
