/**
 * @file terminate.cpp
 * @brief terminate 工具实现——终止当前 mobile use 会话
 *
 * 功能：
 *   定义 MCP 工具 "terminate"：向调用方返回会话终止提示文本。
 *
 * 开发思路：
 *   纯语义化工具：当前实现无实际资源需要释放（provider 为进程级
 *   单例），仅返回固定文本，供 LLM 显式标记任务结束；后续如需
 *   释放设备连接等资源可在 handler 中扩展。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "base.hpp"

namespace tool {

/**
 * @brief 构造 terminate 工具的 ToolDef
 * @return 名为 "terminate" 的工具定义（含 inputSchema 与 handler）
 *
 * 伪代码：handler 忽略参数，直接返回 "Session terminated" 文本结果。
 */
ToolDef makeTerminateTool() {
    mj::Value props = mj::Value::object();

    ToolDef def;
    def.name = "terminate";
    def.description = "Terminate the current mobile use session";
    def.inputSchema = makeSchema(props, {});
    def.handler = [](const mj::Value&) -> mj::Value {
        // 无实际清理动作，仅返回会话终止语义
        return textResult("Session terminated");
    };
    return def;
}

}  // namespace tool
