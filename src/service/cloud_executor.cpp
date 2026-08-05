/**
 * @file cloud_executor.cpp
 * @brief 云手机命令执行器——通过 CPH sync-commands REST API 远程执行 shell
 *
 * 功能：
 *   实现 CloudExecutor：把 shell 命令包装成 JSON 请求体，POST 到华为云
 *   云手机接口 /v1/{projectId}/cloud-phone/phones/sync-commands，
 *   同步等待设备执行完毕并解析 jobs[0] 的结果映射为 ExecResult。
 *
 * 开发思路：
 *   1. 请求体固定 {command:"shell", content:<cmd>, phone_ids:[phoneId]}；
 *      鉴权双模式：有 token 用 X-Auth-Token，否则用 AK/SK 走
 *      SDK-HMAC-SHA256 签名（见 hw_signer.cpp）。
 *   2. 复用手写 JSON 库（mj::Value）与 HTTPS 客户端（net::HttpsClient），
 *      保持全项目零第三方依赖。
 *   3. 云端 job 语义与本地进程语义对齐：status==2 视为成功（exitCode=0），
 *      其余状态把 error_code/error_msg/execute_msg 拼成错误描述；
 *      execute_msg 作为 stdout 等价物存入 result.out。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "cloud_config.hpp"
#include "executor.hpp"
#include "hw_signer.hpp"

#include "../json/json.hpp"
#include "../net/https_client.hpp"

#include <map>

namespace service {

namespace {

/**
 * @class CloudExecutor
 * @brief 云手机后端执行器（持有 CloudConfig，一次配置多次调用）
 *
 * 开发思路：
 *   每个实例绑定一份配置（m_endpoint/m_projectId/m_phoneId/凭据），
 *   runShell 无内部状态可重入；由 Provider 以 unique_ptr 持有管理生命周期。
 */
class CloudExecutor : public Executor {
public:
    explicit CloudExecutor(const CloudConfig& config) : m_config(config) {}

    /** @brief 后端名称："cloud" */
    const char* name() const override { return "cloud"; }

    /**
     * @brief 通过云端 API 执行 shell 命令
     * @param shellCmd 完整 shell 命令串
     * @param timeoutMs HTTP 超时毫秒数（<=0 时取默认 30000）
     * @return ExecResult（execute_msg -> out；status==2 -> exitCode=0）
     *
     * 伪代码：
     *   校验配置 -> 拼 JSON body -> 选鉴权方式拼 header
     *   -> HTTPS POST -> 检查传输错误/HTTP 状态码 -> 解析 JSON
     *   -> 取 jobs[0] -> status==2 成功否则拼错误描述
     */
    ExecResult runShell(const std::string& shellCmd, int timeoutMs) override {
        ExecResult result;
        // 配置不完整直接失败，避免发出注定 401 的请求
        if (!m_config.valid()) {
            result.error = m_config.missingHint();
            return result;
        }

        // 组装请求体：{command:"shell", content:<cmd>, phone_ids:[phoneId]}
        mj::Value body = mj::Value::object();
        body["command"] = "shell";
        body["content"] = shellCmd;
        mj::Value phoneIds = mj::Value::array();
        phoneIds.push(m_config.m_phoneId);
        body["phone_ids"] = phoneIds;
        std::string bodyStr = body.dump();

        // 同步命令执行接口路径（projectId 嵌入路径）
        std::string uri = "/v1/" + m_config.m_projectId + "/cloud-phone/phones/sync-commands";

        // 组装请求头：优先 X-Auth-Token；否则 AK/SK + SDK-HMAC-SHA256 签名
        std::map<std::string, std::string> headers;
        headers["Content-Type"] = "application/json";
        if (!m_config.m_token.empty()) {
            headers["X-Auth-Token"] = m_config.m_token;
        } else {
            std::string sdkDate = hwSdkDateNow();
            headers["X-Sdk-Date"] = sdkDate;
            headers["Authorization"] =
                hwSignAuthorization(m_config.m_ak, m_config.m_sk, "POST", m_config.m_endpoint, uri,
                                    bodyStr, sdkDate);
        }

        // 发起 HTTPS POST（443 端口，走项目手写 HttpsClient）
        net::HttpsClient client(timeoutMs > 0 ? timeoutMs : 30000);
        net::HttpResponse resp = client.post(m_config.m_endpoint, 443, uri, headers, bodyStr);
        if (!resp.error.empty()) {
            result.error = "cloud request failed: " + resp.error;
            return result;
        }
        if (resp.status < 200 || resp.status >= 300) {
            result.error = "cloud api http " + std::to_string(resp.status) + ": " +
                           resp.body.substr(0, 512);
            return result;
        }

        // 解析响应 JSON（解析失败视为框架级错误）
        mj::Value doc;
        try {
            doc = mj::Value::parse(resp.body);
        } catch (const std::exception& e) {
            result.error = std::string("cloud api bad json: ") + e.what();
            return result;
        }

        // 取 jobs 数组的第一个任务结果（本接口单手机单任务）
        const mj::Value& jobs = doc["jobs"];
        if (!jobs.isArray() || jobs.size() == 0) {
            result.error = "cloud api returned no jobs: " + resp.body.substr(0, 512);
            return result;
        }

        const mj::Value& job = jobs.asArray()[0];
        int status = job["status"].asInt(-100);
        std::string executeMsg = job["execute_msg"].asString();
        std::string errorCode = job["error_code"].asString();
        std::string errorMsg = job["error_msg"].asString();

        // 状态映射：2 = 执行成功；execute_msg 等价于本地 stdout
        result.out = executeMsg;
        if (status == 2) {
            result.exitCode = 0;
        } else {
            result.exitCode = -1;
            result.error = "cloud command failed (status=" + std::to_string(status) + ") " +
                           errorCode + " " + errorMsg + " " + executeMsg;
        }
        return result;
    }

private:
    CloudConfig m_config;  // 云端连接与鉴权配置（构造时注入）
};

}  // namespace

/**
 * @brief 创建云端执行器实例
 * @param config 云端配置（按值拷入实例）
 * @return new 出来的 Executor，调用方（Provider）负责 delete
 */
Executor* createCloudExecutor(const CloudConfig& config) {
    return new CloudExecutor(config);
}

}  // namespace service
