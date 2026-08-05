/**
 * @file sha256.hpp
 * @brief SHA-256 / HMAC-SHA-256 / Base64URL——零第三方依赖的密码学基础组件
 *
 * 功能：
 *   提供 SHA-256 摘要（二进制与十六进制两种形式）、HMAC-SHA-256 消息认证码，
 *   以及 JWT 所需的 Base64URL（无填充）编解码。
 *
 * 开发思路：
 *   1. 为满足"零依赖全手写"约束（不引入 OpenSSL），按 FIPS 180-4 手写
 *      SHA-256 压缩函数与流式 update/final 接口。
 *   2. HMAC 按 RFC 2104 实现：密钥超 64 字节先取摘要，ipad/opad 两次哈希。
 *   3. Base64URL 与标准 Base64 的差别仅在码表（- _ 替换 + /）且不带填充，
 *      专为 JWT 段编码服务，故与 SHA-256 放在同一模块。
 *
 * @author hubin
 * @date 2026-08-05
 */
#pragma once

#include <cstdint>
#include <string>

namespace util {

/**
 * @brief 计算 SHA-256 并以小写十六进制字符串返回
 * @param input 输入数据
 * @return 64 字符十六进制摘要
 */
std::string sha256Hex(const std::string& input);
/**
 * @brief 计算 SHA-256 二进制摘要
 * @param data 输入字节流
 * @param len 字节长度
 * @param out 输出 32 字节摘要
 */
void sha256(const unsigned char* data, size_t len, unsigned char out[32]);

/**
 * @brief HMAC-SHA-256（RFC 2104）
 * @param key 密钥
 * @param message 消息
 * @return 32 字节二进制 MAC
 */
std::string hmacSha256(const std::string& key, const std::string& message);

/**
 * @brief Base64URL 编码（RFC 4648 §5，无 '=' 填充，供 JWT 使用）
 * @param data 输入字节流
 * @param len 字节长度
 */
std::string base64UrlEncode(const unsigned char* data, size_t len);
/**
 * @brief Base64URL 解码
 * @param in Base64URL 字符串（遇 '=' 停止）
 * @param out 解码输出
 * @return 成功返回 true；含非法字符返回 false
 */
bool base64UrlDecode(const std::string& in, std::string& out);

}  // namespace util
