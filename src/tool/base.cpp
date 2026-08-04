#include "base.hpp"

#include <memory>

namespace tool {

namespace {
std::unique_ptr<service::MobileUseProvider> gProvider;
}

void initProvider(service::Backend defaultBackend, const service::CloudConfig& cloudConfig) {
    gProvider = std::make_unique<service::MobileUseProvider>(defaultBackend);
    gProvider->setCloudConfig(cloudConfig);
}

service::MobileUseProvider& provider() {
    return *gProvider;
}

mj::Value textResult(const std::string& text) {
    mj::Value result = mj::Value::object();
    mj::Value content = mj::Value::array();
    mj::Value item = mj::Value::object();
    item["type"] = "text";
    item["text"] = text;
    content.push(item);
    result["content"] = content;
    result["isError"] = false;
    return result;
}

mj::Value errorResult(const std::string& msg) {
    mj::Value result = textResult(msg);
    result["isError"] = true;
    return result;
}

mj::Value imageResult(const std::string& base64Png) {
    mj::Value result = mj::Value::object();
    mj::Value content = mj::Value::array();
    mj::Value item = mj::Value::object();
    item["type"] = "image";
    item["data"] = base64Png;
    item["mimeType"] = "image/png";
    content.push(item);
    result["content"] = content;
    result["isError"] = false;
    return result;
}

mj::Value propNumber(const std::string& desc) {
    mj::Value p = mj::Value::object();
    p["type"] = "number";
    p["description"] = desc;
    return p;
}

mj::Value propString(const std::string& desc) {
    mj::Value p = mj::Value::object();
    p["type"] = "string";
    p["description"] = desc;
    return p;
}

mj::Value propBool(const std::string& desc) {
    mj::Value p = mj::Value::object();
    p["type"] = "boolean";
    p["description"] = desc;
    return p;
}

mj::Value makeSchema(const mj::Value& properties, const std::vector<std::string>& required) {
    mj::Value props = properties;
    mj::Value backend = mj::Value::object();
    backend["type"] = "string";
    mj::Value enumVals = mj::Value::array();
    enumVals.push("adb");
    enumVals.push("cloud");
    backend["enum"] = enumVals;
    backend["description"] =
        "Execution backend: 'adb' runs shell commands locally on the device (default), "
        "'cloud' dispatches via Huawei Cloud CPH RunSyncCommand API";
    props["backend"] = backend;

    mj::Value schema = mj::Value::object();
    schema["type"] = "object";
    schema["properties"] = props;
    mj::Value req = mj::Value::array();
    for (const auto& r : required) req.push(r);
    schema["required"] = req;
    return schema;
}

service::Backend pickBackend(const mj::Value& args) {
    return provider().resolveBackend(args);
}

bool getInt(const mj::Value& args, const char* key, int& out, std::string& err) {
    if (!args.isObject() || !args.has(key) || !args[key].isNumber()) {
        err = std::string(key) + " is required and must be a number";
        return false;
    }
    out = args[key].asInt();
    return true;
}

bool getRequiredString(const mj::Value& args, const char* key, std::string& out,
                       std::string& err) {
    if (!args.isObject() || !args.has(key) || !args[key].isString() ||
        args[key].asString().empty()) {
        err = std::string(key) + " is required and must be a non-empty string";
        return false;
    }
    out = args[key].asString();
    return true;
}

}  // namespace tool
