#include "cloud_config.hpp"
#include "executor.hpp"
#include "hw_signer.hpp"

#include "../json/json.hpp"
#include "../net/https_client.hpp"

#include <map>

namespace service {

namespace {

class CloudExecutor : public Executor {
public:
    explicit CloudExecutor(const CloudConfig& config) : config_(config) {}

    const char* name() const override { return "cloud"; }

    ExecResult runShell(const std::string& shellCmd, int timeoutMs) override {
        ExecResult result;
        if (!config_.valid()) {
            result.error = config_.missingHint();
            return result;
        }

        mj::Value body = mj::Value::object();
        body["command"] = "shell";
        body["content"] = shellCmd;
        mj::Value phoneIds = mj::Value::array();
        phoneIds.push(config_.phoneId);
        body["phone_ids"] = phoneIds;
        std::string bodyStr = body.dump();

        std::string uri = "/v1/" + config_.projectId + "/cloud-phone/phones/sync-commands";

        std::map<std::string, std::string> headers;
        headers["Content-Type"] = "application/json";
        if (!config_.token.empty()) {
            headers["X-Auth-Token"] = config_.token;
        } else {
            std::string sdkDate = hwSdkDateNow();
            headers["X-Sdk-Date"] = sdkDate;
            headers["Authorization"] =
                hwSignAuthorization(config_.ak, config_.sk, "POST", config_.endpoint, uri,
                                    bodyStr, sdkDate);
        }

        net::HttpsClient client(timeoutMs > 0 ? timeoutMs : 30000);
        net::HttpResponse resp = client.post(config_.endpoint, 443, uri, headers, bodyStr);
        if (!resp.error.empty()) {
            result.error = "cloud request failed: " + resp.error;
            return result;
        }
        if (resp.status < 200 || resp.status >= 300) {
            result.error = "cloud api http " + std::to_string(resp.status) + ": " +
                           resp.body.substr(0, 512);
            return result;
        }

        mj::Value doc;
        try {
            doc = mj::Value::parse(resp.body);
        } catch (const std::exception& e) {
            result.error = std::string("cloud api bad json: ") + e.what();
            return result;
        }

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
    CloudConfig config_;
};

}  // namespace

Executor* createCloudExecutor(const CloudConfig& config) {
    return new CloudExecutor(config);
}

}  // namespace service
