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

    mj::Value req;
    try {
        req = mj::Value::parse(requestJson);
    } catch (const std::exception& e) {
        responseJson = makeError(mj::Value(), -32700, std::string("parse error: ") + e.what()).dump();
        return true;
    }

    if (!req.isObject()) {
        responseJson = makeError(mj::Value(), -32600, "invalid request: not an object").dump();
        return true;
    }

    std::string method = req["method"].asString();
    if (method.empty()) {
        responseJson = makeError(req["id"], -32600, "invalid request: missing method").dump();
        return true;
    }

    bool isNotification = !req.has("id") || req["id"].isNull();

    if (method.compare(0, 13, "notifications/") == 0) return false;

    if (isNotification) return false;

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
