/**
 * @file base.cpp
 * @brief MCP 工具层公共基础设施实现——结果构造、schema 生成与参数解析
 *
 * 功能：
 *   实现 base.hpp 声明的全部公共函数：全局 provider 的初始化与获取、
 *   MCP 三类结果（文本/错误/图片）的构造、JSON Schema 属性与完整 schema
 *   的生成（makeSchema 自动注入 backend 枚举）、以及参数解析辅助函数。
 *
 * 开发思路：
 *   1. 全局 provider 用 unique_ptr 持有于匿名命名空间，initProvider 由
 *      main/jni_bridge 在启动时调用一次，工具 handler 通过 provider() 取引用，
 *      避免传参穿透所有工具。
 *   2. 结果构造严格遵循 MCP 规范：{content:[{type:"text"|"image",...}],isError:bool}。
 *   3. makeSchema 以拷贝方式合并调用方属性表并统一追加 backend 属性，
 *      各工具无需感知 backend 的存在。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "base.hpp"

#include <memory>

namespace tool {

namespace {
/** @brief 全局服务提供者单例（initProvider 前为空指针，工具调用前必须已初始化） */
std::unique_ptr<service::MobileUseProvider> gProvider;
}

/**
 * @brief 初始化全局服务提供者
 * @param defaultBackend 默认执行后端（adb 本地命令 / cloud 华为云 CPH）
 * @param cloudConfig    云端后端配置（来自环境变量）
 *
 * 伪代码：创建 MobileUseProvider 实例 -> 注入云端配置 -> 存入全局指针
 */
void initProvider(service::Backend defaultBackend, const service::CloudConfig& cloudConfig) {
    gProvider = std::make_unique<service::MobileUseProvider>(defaultBackend);
    gProvider->setCloudConfig(cloudConfig);
}

/**
 * @brief 获取全局服务提供者引用
 * @return MobileUseProvider 引用（调用前必须已 initProvider）
 */
service::MobileUseProvider& provider() {
    return *gProvider;
}

/**
 * @brief 构造 MCP 文本结果
 * @param text 文本内容
 * @return {content:[{type:"text",text:...}], isError:false}
 */
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

/**
 * @brief 构造 MCP 错误结果
 * @param msg 错误描述
 * @return 复用 textResult 结构，仅将 isError 置为 true
 */
mj::Value errorResult(const std::string& msg) {
    mj::Value result = textResult(msg);
    result["isError"] = true;
    return result;
}

/**
 * @brief 构造 MCP 图片结果
 * @param base64Png base64 编码的 PNG 数据
 * @return {content:[{type:"image",data:...,mimeType:"image/png"}], isError:false}
 */
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

/**
 * @brief 构造 number 类型属性定义
 * @param desc 参数说明（展示给 LLM）
 */
mj::Value propNumber(const std::string& desc) {
    mj::Value p = mj::Value::object();
    p["type"] = "number";
    p["description"] = desc;
    return p;
}

/**
 * @brief 构造 string 类型属性定义
 * @param desc 参数说明（展示给 LLM）
 */
mj::Value propString(const std::string& desc) {
    mj::Value p = mj::Value::object();
    p["type"] = "string";
    p["description"] = desc;
    return p;
}

/**
 * @brief 构造 boolean 类型属性定义
 * @param desc 参数说明（展示给 LLM）
 */
mj::Value propBool(const std::string& desc) {
    mj::Value p = mj::Value::object();
    p["type"] = "boolean";
    p["description"] = desc;
    return p;
}

/**
 * @brief 构造完整 inputSchema，自动注入 backend 枚举属性
 * @param properties 工具自身的参数属性表
 * @param required   必填参数名列表
 * @return {type:"object", properties:{... , backend:{enum:[adb,cloud]}}, required:[...]}
 *
 * 伪代码：
 *   1. 拷贝调用方属性表，追加 backend 属性（type=string, enum=[adb,cloud]，
 *      附双后端差异说明）；
 *   2. 组装 object 类型 schema，按序写入 required 数组。
 */
mj::Value makeSchema(const mj::Value& properties, const std::vector<std::string>& required) {
    mj::Value props = properties;
    // 为每个工具统一注入 backend 参数：adb=本地执行 shell 命令（默认），cloud=华为云 CPH API
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

/**
 * @brief 从调用参数解析执行后端
 * @param args 调用参数（可能含 backend 字段）
 * @return 参数指定的后端；缺省/非法值时回落到全局默认后端
 */
service::Backend pickBackend(const mj::Value& args) {
    return provider().resolveBackend(args);
}

/**
 * @brief 解析必填整数参数
 * @param args 调用参数对象
 * @param key  参数名
 * @param out  解析成功时的值输出
 * @param err  解析失败时的错误信息输出
 * @return 参数存在且为数字返回 true，否则 false
 *
 * 伪代码：校验 args 为对象且含 key 且为 number -> 否则回填 err；
 *         通过则 asInt 取整写入 out。
 */
bool getInt(const mj::Value& args, const char* key, int& out, std::string& err) {
    // 参数校验：必须是对象、含该键、且值为数字
    if (!args.isObject() || !args.has(key) || !args[key].isNumber()) {
        err = std::string(key) + " is required and must be a number";
        return false;
    }
    out = args[key].asInt();
    return true;
}

/**
 * @brief 解析必填非空字符串参数
 * @param args 调用参数对象
 * @param key  参数名
 * @param out  解析成功时的值输出
 * @param err  解析失败时的错误信息输出
 * @return 参数存在且为非空字符串返回 true，否则 false
 */
bool getRequiredString(const mj::Value& args, const char* key, std::string& out,
                       std::string& err) {
    // 参数校验：必须是对象、含该键、值为字符串且非空
    if (!args.isObject() || !args.has(key) || !args[key].isString() ||
        args[key].asString().empty()) {
        err = std::string(key) + " is required and must be a non-empty string";
        return false;
    }
    out = args[key].asString();
    return true;
}

}  // namespace tool
