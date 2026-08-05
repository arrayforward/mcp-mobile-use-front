/**
 * @file text_input.cpp
 * @brief text_input 工具实现——在云手机当前焦点处输入文本
 *
 * 功能：
 *   定义 MCP 工具 "text_input"：向设备当前焦点输入文本。
 *   ASCII 文本直接使用 input text 命令；非 ASCII（如中文）经云手机
 *   输入法广播发送，要求设备已安装配套 IME。
 *
 * 开发思路：
 *   单一必填字符串参数 text；具体 ASCII/非 ASCII 的分发逻辑封装在
 *   provider().inputText 中，工具层只负责参数校验与结果包装。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "base.hpp"

namespace tool {

/**
 * @brief 构造 text_input 工具的 ToolDef
 * @return 名为 "text_input" 的工具定义（含 inputSchema 与 handler）
 *
 * 伪代码：
 *   handler：解析必填 text -> pickBackend -> provider().inputText ->
 *   成功返回文本结果，失败返回 errorResult。
 */
ToolDef makeTextInputTool() {
    mj::Value props = mj::Value::object();
    props["text"] = propString("The text to input at the current focus");

    ToolDef def;
    def.name = "text_input";
    def.description =
        "Input text at the current focus on the cloud phone. ASCII text uses 'input text' "
        "directly; non-ASCII text (e.g. Chinese) is sent via the cloud phone input method "
        "broadcast and requires the matching IME to be installed";
    def.inputSchema = makeSchema(props, {"text"});
    def.handler = [](const mj::Value& args) -> mj::Value {
        // 参数解析：text 为必填非空字符串
        std::string text, err;
        if (!getRequiredString(args, "text", text, err)) return errorResult(err);
        if (!provider().inputText(pickBackend(args), text, err)) return errorResult(err);
        return textResult("Input text successfully");
    };
    return def;
}

}  // namespace tool
