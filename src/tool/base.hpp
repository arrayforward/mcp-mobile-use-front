/**
 * @file base.hpp
 * @brief MCP 工具层公共基础设施——ToolDef 定义、schema 构造与参数解析工具
 *
 * 功能：
 *   定义工具描述结构 ToolDef（name/description/inputSchema/handler），
 *   声明工具注册入口 allTools()、MCP 结果构造器（textResult/errorResult/imageResult）、
 *   JSON Schema 构造器（makeSchema/propXxx）以及参数解析辅助函数
 *   （pickBackend/getInt/getRequiredString）。
 *
 * 开发思路：
 *   1. 每个工具实现为一个独立的 makeXxxTool() 工厂函数（见各工具 .cpp），
 *      返回统一的 ToolDef，registry.cpp 集中汇总，新增工具只需加一个文件
 *      并在 registry 中登记一行。
 *   2. makeSchema 自动为所有工具注入 backend 枚举属性（adb/cloud），
 *      使每个工具都支持调用方选择执行后端，无需各工具重复声明。
 *   3. 参数解析采用"取值 + 出错信息回填"模式（out 参数 + err 引用），
 *      与 MCP 协议 errorResult 返回约定配合，避免异常开销。
 *
 * @author hubin
 * @date 2026-08-05
 */
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../json/json.hpp"
#include "../service/provider.hpp"

namespace tool {

/** @brief 初始化全局服务提供者（默认后端 + 云端配置），须在工具调用前执行一次 */
void initProvider(service::Backend defaultBackend, const service::CloudConfig& cloudConfig);
/** @brief 获取全局服务提供者引用（执行 tap/swipe/shell 等实际设备操作的门面） */
service::MobileUseProvider& provider();

/**
 * @struct ToolDef
 * @brief MCP 工具描述结构：名称、说明、输入 schema 与处理函数的四元组
 *
 * 开发思路：
 *   与 MCP 协议 tools/list 响应字段一一对应：name/description 直接透传，
 *   inputSchema 为 JSON Schema 对象（由 makeSchema 构造，自动含 backend 属性），
 *   handler 接收 tools/call 的 arguments（mj::Value），返回符合 MCP 规范的
 *   content 数组结果（textResult/errorResult/imageResult）。
 *
 * @author hubin
 * @date 2026-08-05
 */
struct ToolDef {
    std::string name;         ///< 工具名（MCP tools/call 的 name 字段）
    std::string description;  ///< 工具功能说明（展示给 LLM 的工具描述）
    mj::Value inputSchema;    ///< 输入参数 JSON Schema（object 类型，含 properties/required）
    /// 工具处理函数：入参为调用参数对象，返回 MCP content 结果
    std::function<mj::Value(const mj::Value&)> handler;
};

/** @brief 汇总全部已注册工具，供 MCP 服务层 tools/list 使用 */
std::vector<ToolDef> allTools();

/** @brief 构造 MCP 文本类型结果（isError=false） */
mj::Value textResult(const std::string& text);
/** @brief 构造 MCP 错误类型结果（文本内容 + isError=true） */
mj::Value errorResult(const std::string& msg);
/** @brief 构造 MCP 图片类型结果（base64 编码 PNG） */
mj::Value imageResult(const std::string& base64Png);

/**
 * @brief 由属性表与必填字段列表构造完整的 inputSchema
 *
 * 自动为所有工具注入 backend 字符串枚举属性（adb / cloud），
 * 使每个工具都支持调用方显式选择执行后端。
 *
 * @param properties 各参数的属性定义（propNumber/propString/propBool 生成）
 * @param required   必填参数名列表
 * @return 完整的 JSON Schema object
 */
mj::Value makeSchema(const mj::Value& properties, const std::vector<std::string>& required);
/** @brief 构造 number 类型属性定义 */
mj::Value propNumber(const std::string& desc);
/** @brief 构造 string 类型属性定义 */
mj::Value propString(const std::string& desc);
/** @brief 构造 boolean 类型属性定义 */
mj::Value propBool(const std::string& desc);

/** @brief 从参数中解析执行后端（缺省时回落到全局默认后端） */
service::Backend pickBackend(const mj::Value& args);

/**
 * @brief 解析必填整数参数
 * @param args 调用参数对象
 * @param key  参数名
 * @param out  解析结果输出
 * @param err  失败时的错误信息输出
 * @return 参数存在且为数字时返回 true，否则回填 err 返回 false
 */
bool getInt(const mj::Value& args, const char* key, int& out, std::string& err);

/**
 * @brief 解析必填非空字符串参数
 * @param args 调用参数对象
 * @param key  参数名
 * @param out  解析结果输出
 * @param err  失败时的错误信息输出
 * @return 参数存在且为非空字符串时返回 true，否则回填 err 返回 false
 */
bool getRequiredString(const mj::Value& args, const char* key, std::string& out,
                       std::string& err);

}  // namespace tool
