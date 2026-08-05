/**
 * @file protocol.cpp
 * @brief MCP 协议层实现——JSON-RPC 2.0 消息解析、分发与响应封装
 *
 * 功能：
 *   实现 protocol.hpp 声明的 Protocol 类：initialize/ping/tools/list/tools/call
 *   分发逻辑、工具注册表遍历、JSON-RPC 标准错误码处理。
 *
 * 开发思路：
 *   1. handleMessage 作为唯一入口，先做语法与结构校验（-32700/-32600），
 *      再做通知过滤，最后 dispatch；所有异常统一捕获转 -32603，
 *      保证任何坏输入都不会让进程崩溃。
 *   2. 工具调用时捕获工具 handler 抛出的异常，转为 MCP 错误结果
 *      （isError=true 的 content），符合 MCP 对工具错误的约定。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "protocol.hpp"

#include "../tool/base.hpp"

namespace mcp {

Protocol::Protocol() {}

const char* Protocol::protocolVersion() { return "2024-11-05"; }
const char* Protocol::serverName() { return "mcp_mobile_use"; }
const char* Protocol::serverVersion() { return "0.1.0"; }

mj::Value Protocol::makeResult(const mj::Value& id, const mj::Value& result) {
    mj::Value resp = mj::Value::object();
    resp["jsonrpc"] = "2.0";
    resp["id"] = id;
    resp["result"] = result;
    return resp;
}

mj::Value Protocol::makeError(const mj::Value& id, int code, const std::string& message) {
    mj::Value resp = mj::Value::object();
    resp["jsonrpc"] = "2.0";
    resp["id"] = id;
    mj::Value err = mj::Value::object();
    err["code"] = code;
    err["message"] = message;
    resp["error"] = err;
    return resp;
}

bool Protocol::handleMessage(const std::string& requestJson, std::string& responseJson) {
    responseJson.clear();

    // 第一步：JSON 语法解析，失败返回 -32700（parse error）
    mj::Value req;
    try {
        req = mj::Value::parse(requestJson);
    } catch (const std::exception& e) {
        responseJson = makeError(mj::Value(), -32700, std::string("parse error: ") + e.what()).dump();
        return true;
    }

    // 第二步：结构校验，JSON-RPC 请求必须是对象
    if (!req.isObject()) {
        responseJson = makeError(mj::Value(), -32600, "invalid request: not an object").dump();
        return true;
    }

    // 第三步：method 字段必填
    std::string method = req["method"].asString();
    if (method.empty()) {
        responseJson = makeError(req["id"], -32600, "invalid request: missing method").dump();
        return true;
    }

    // 无 id 或 id 为 null 视为通知（notification），规范要求不回响应
    bool isNotification = !req.has("id") || req["id"].isNull();

    // notifications/* 前缀的消息（如 notifications/initialized）直接忽略
    if (method.compare(0, 13, "notifications/") == 0) return false;

    if (isNotification) return false;

    // 第四步：分发执行；任何内部异常兜底为 -32603（internal error）
    mj::Value resp;
    try {
        resp = dispatch(req["id"], method, req["params"]);
    } catch (const std::exception& e) {
        resp = makeError(req["id"], -32603, std::string("internal error: ") + e.what());
    }
    responseJson = resp.dump();
    return true;
}

mj::Value Protocol::dispatch(const mj::Value& id, const std::string& method,
                             const mj::Value& params) {
    if (method == "initialize") return makeResult(id, onInitialize());
    if (method == "ping") return makeResult(id, mj::Value::object());
    if (method == "tools/list") return makeResult(id, onToolsList());
    if (method == "tools/call") return makeResult(id, onToolsCall(params));
    return makeError(id, -32601, "method not found: " + method);
}

mj::Value Protocol::onInitialize() {
    mj::Value result = mj::Value::object();
    result["protocolVersion"] = protocolVersion();
    // capabilities：声明服务支持工具能力，listChanged=false 表示不推送工具变更通知
    mj::Value capabilities = mj::Value::object();
    mj::Value tools = mj::Value::object();
    tools["listChanged"] = false;
    capabilities["tools"] = tools;
    result["capabilities"] = capabilities;
    mj::Value serverInfo = mj::Value::object();
    serverInfo["name"] = serverName();
    serverInfo["version"] = serverVersion();
    result["serverInfo"] = serverInfo;
    return result;
}

mj::Value Protocol::onToolsList() {
    mj::Value result = mj::Value::object();
    mj::Value tools = mj::Value::array();
    // 遍历工具注册表，按注册顺序输出每个工具的元信息
    for (const auto& def : tool::allTools()) {
        mj::Value t = mj::Value::object();
        t["name"] = def.name;
        t["description"] = def.description;
        t["inputSchema"] = def.inputSchema;
        tools.push(t);
    }
    result["tools"] = tools;
    return result;
}

mj::Value Protocol::onToolsCall(const mj::Value& params) {
    std::string name = params["name"].asString();
    if (name.empty()) {
        return tool::errorResult("tools/call params.name is required");
    }
    mj::Value args = params["arguments"];
    // 按名字线性查找工具并调用；工具异常转为 MCP 错误结果而非 JSON-RPC 错误
    for (const auto& def : tool::allTools()) {
        if (def.name == name) {
            try {
                return def.handler(args);
            } catch (const std::exception& e) {
                return tool::errorResult(std::string("tool ") + name +
                                         " threw exception: " + e.what());
            }
        }
    }
    return tool::errorResult("unknown tool: " + name);
}

}  // namespace mcp
