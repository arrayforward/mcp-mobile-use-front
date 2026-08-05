#include "base.hpp"

#include "../service/executor.hpp"

namespace tool {

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
        std::string command, err;
        if (!getRequiredString(args, "command", command, err)) return errorResult(err);

        int timeoutMs = 30000;
        if (args.has("timeout_ms") && args["timeout_ms"].isNumber())
            timeoutMs = args["timeout_ms"].asInt(30000);

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
