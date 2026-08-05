/**
 * @file hw_signer.hpp
 * @brief 华为云 SDK-HMAC-SHA256 请求签名接口声明
 *
 * 功能：
 *   声明云手机 API 的 AK/SK 签名三件套：生成 Authorization 头、
 *   生成 X-Sdk-Date 时间戳、查询签名功能是否可用。
 *
 * 开发思路：
 *   签名依赖 OpenSSL 的 HMAC/SHA256，用 MCP_WITH_OPENSSL 宏做条件编译：
 *   定义时走真实签名实现；未定义时提供同名空实现（返回空串/false），
 *   保证无 OpenSSL 环境也能编译链接，由上层决定降级策略（如改用 token）。
 *
 * @author hubin
 * @date 2026-08-05
 */
#pragma once

#include <map>
#include <string>

namespace service {

/**
 * @brief 计算华为云 SDK-HMAC-SHA256 签名的 Authorization 头
 * @param ak Access Key @param sk Secret Key
 * @param method HTTP 方法（如 "POST"） @param host 目标域名
 * @param uri 请求路径 @param body 请求体原文 @param sdkDate X-Sdk-Date 时间戳
 * @return "SDK-HMAC-SHA256 Access=..., SignedHeaders=..., Signature=..."
 */
std::string hwSignAuthorization(const std::string& ak, const std::string& sk,
                                const std::string& method, const std::string& host,
                                const std::string& uri, const std::string& body,
                                const std::string& sdkDate);

/** @brief 生成当前 UTC 时间戳（格式 "%Y%m%dT%H%M%SZ"，用于 X-Sdk-Date） */
std::string hwSdkDateNow();

/** @brief 查询签名实现是否可用（编译期是否启用 OpenSSL） */
bool hwSignerAvailable();

}  // namespace service
