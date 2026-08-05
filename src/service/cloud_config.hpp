/**
 * @file cloud_config.hpp
 * @brief 云手机（CPH）后端配置——从 CPH_* 环境变量读取连接与鉴权参数
 *
 * 功能：
 *   定义 CloudConfig 结构体，集中保存云端 API 的 m_endpoint、m_projectId、
 *   m_phoneId 以及两种互斥的鉴权方式（X-Auth-Token 或 AK/SK 签名），
 *   提供 valid() 完整性校验、missingHint() 缺参提示与 fromEnv() 环境加载。
 *
 * 开发思路：
 *   1. 配置不落盘、不进配置文件，全部通过环境变量注入，符合十二要素应用
 *      与容器化部署习惯，也避免密钥误提交进仓库。
 *   2. 支持双鉴权：token 简单场景直接用；生产推荐 AK/SK + SDK-HMAC-SHA256
 *      签名（见 hw_signer），valid() 只要求二者其一齐备。
 *
 * @author hubin
 * @date 2026-08-05
 */
#pragma once

#include <string>

namespace service {

/**
 * @struct CloudConfig
 * @brief 云手机后端连接配置
 *
 * 开发思路：
 *   纯数据结构 + 轻量校验；m_endpoint/m_projectId/m_phoneId 为必填定位信息，
 *   m_token 与 m_ak+m_sk 两组鉴权凭据满足任意一组即视为有效。
 */
struct CloudConfig {
    std::string m_endpoint;    // API 网关域名（如 cph.cn-xxx.myhuaweicloud.com）
    std::string m_projectId;   // 华为云项目 ID（拼接进 URI 路径）
    std::string m_phoneId;     // 目标云手机实例 ID（body 的 phone_ids）
    std::string m_token;       // X-Auth-Token 鉴权（与 AK/SK 二选一）
    std::string m_ak;          // Access Key（SDK-HMAC-SHA256 签名用）
    std::string m_sk;          // Secret Key（SDK-HMAC-SHA256 签名用）

    /**
     * @brief 校验配置是否可用
     * @return true 表示定位信息齐全且至少一组鉴权凭据完整
     */
    bool valid() const {
        return !m_endpoint.empty() && !m_projectId.empty() && !m_phoneId.empty() &&
               (!m_token.empty() || (!m_ak.empty() && !m_sk.empty()));
    }

    /** @brief 返回缺失配置时的英文提示文案（列出需设置的环境变量） */
    std::string missingHint() const;

    /** @brief 从 CPH_* 环境变量构造配置（缺省字段为空串） */
    static CloudConfig fromEnv();
};

}  // namespace service
