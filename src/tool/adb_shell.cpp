/**
 * @file adb_shell.cpp
 * @brief adb_shell 工具实现——在设备上执行任意 adb shell 命令
 *
 * 功能：
 *   定义 MCP 工具 "adb_shell"：通用底层接口，对齐 'adb shell <command>'，
 *   返回命令的 exit_code/timed_out/stdout/stderr 完整执行信息。
 *
 * 开发思路：
 *   1. 该工具可执行任意命令，是高危能力，描述中明确警告：对外暴露
 *      MCP 端点时务必启用 auth 保护。
 *   2. 常用操作（点击/滑动/截图等）应优先使用专用工具，本工具作为兜底。
 *   3. 支持可选 timeout_ms 控制命令超时（默认 30000ms），防止长命令
 *      阻塞 MCP 会话。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "base.hpp"

#include "../service/executor.hpp"

namespace tool {

/**
 * @brief 构造 adb_shell 工具的 ToolDef
 * @return 名为 "adb_shell" 的工具定义（含 inputSchema 与 handler）
 *
 * 伪代码：
 *   handler：解析必填 command -> 读可选 timeout_ms（默认 30000）->
 *   pickBackend -> provider().runShell 执行命令 ->
 *   将 exit_code/timed_out/stdout/stderr 组装为 JSON 文本返回。
 */
ToolDef makeAdbShellTool() {
    mj::Value props = mj::Value::object();
    props["command"] =
        propString("The shell command to execute on the device, e.g. \"ls -l /sdcard\" or "
                   "\"getprop ro.build.version.release\"");
    props["timeout_ms"] = propNumber("Command timeout in milliseconds, default 30000");

    ToolDef def;
    def.name = "adb_shell";
    def.description =
        "Execute a standard adb shell command on the device and return its output. "
        "This is the generic low-level interface aligned with 'adb shell <command>'; "
        "use the specialized tools (tap, swipe, take_screenshot, ...) for common "
        "operations. WARNING: this tool executes arbitrary commands, keep the MCP "
        "endpoint protected (auth) when exposed to untrusted clients";
    def.inputSchema = makeSchema(props, {"command"});
    def.handler = [](const mj::Value& args) -> mj::Value {
        // 参数解析：command 为必填非空字符串（任意 shell 命令，高危）
        std::string command, err;
        if (!getRequiredString(args, "command", command, err)) return errorResult(err);

        // 可选参数：命令超时时间，缺省 30 秒
        int timeoutMs = 30000;
        if (args.has("timeout_ms") && args["timeout_ms"].isNumber())
            timeoutMs = args["timeout_ms"].asInt(30000);

        // 执行命令并收集完整结果（退出码/是否超时/标准输出/标准错误）
        service::ExecResult result;
        if (!provider().runShell(pickBackend(args), command, timeoutMs, result, err))
            return errorResult(err);

        mj::Value info = mj::Value::object();
        info["command"] = command;
        info["exit_code"] = result.exitCode;
        info["timed_out"] = result.timedOut;
        info["stdout"] = result.out;
        info["stderr"] = result.err;
        return textResult(info.dump());
    };
    return def;
}

}  // namespace tool
