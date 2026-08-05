/**
 * @file cloud_config.cpp
 * @brief CloudConfig 的实现：环境变量读取与缺参提示
 *
 * 功能：
 *   实现 CloudConfig::fromEnv()（读取 CPH_ENDPOINT / CPH_PROJECT_ID /
 *   CPH_PHONE_ID / CPH_TOKEN / CPH_AK / CPH_SK）与 missingHint()。
 *
 * 开发思路：
 *   用匿名命名空间的 env() 小工具封装 getenv，把 nullptr 归一化为空串，
 *   避免上层到处判空；missingHint 返回固定文案，直接拼进错误响应。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "cloud_config.hpp"

#include <cstdlib>

namespace service {

namespace {

/**
 * @brief 读取环境变量并归一化为 std::string
 * @param name 环境变量名
 * @return 变量值；未设置时返回空串
 * 伪代码：getenv -> 为 nullptr 则返回 ""，否则返回原值
 */
std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

}  // namespace

/** @brief 固定提示文案：告知需设置哪些 CPH_* 环境变量 */
std::string CloudConfig::missingHint() const {
    return "cloud backend not configured; set env CPH_ENDPOINT, CPH_PROJECT_ID, "
           "CPH_PHONE_ID and either CPH_TOKEN or CPH_AK+CPH_SK";
}

/**
 * @brief 从环境变量加载云端配置
 * @return 填充后的 CloudConfig（未设置的环境变量对应字段为空串）
 * 伪代码：逐字段 env("CPH_XXX") 赋值 -> 返回 cfg
 */
CloudConfig CloudConfig::fromEnv() {
    CloudConfig cfg;
    cfg.m_endpoint = env("CPH_ENDPOINT");
    cfg.m_projectId = env("CPH_PROJECT_ID");
    cfg.m_phoneId = env("CPH_PHONE_ID");
    cfg.m_token = env("CPH_TOKEN");
    cfg.m_ak = env("CPH_AK");
    cfg.m_sk = env("CPH_SK");
    return cfg;
}

}  // namespace service
