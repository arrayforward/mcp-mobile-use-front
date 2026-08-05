/**
 * @file close_app.cpp
 * @brief close_app 工具实现——按包名强制停止云手机上的应用
 *
 * 功能：
 *   定义 MCP 工具 "close_app"：接收 package_name 参数，
 *   经全局 provider 强制停止对应应用（adb 后端为 am force-stop）。
 *
 * 开发思路：
 *   与 launch_app 结构对称，单一必填字符串参数；
 *   强制停止命令的拼接封装在 provider().closeApp 中。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "base.hpp"

namespace tool {

/**
 * @brief 构造 close_app 工具的 ToolDef
 * @return 名为 "close_app" 的工具定义（含 inputSchema 与 handler）
 *
 * 伪代码：
 *   handler：解析必填 package_name -> pickBackend ->
 *   provider().closeApp -> 成功返回含包名的文本，失败返回 errorResult。
 */
ToolDef makeCloseAppTool() {
    mj::Value props = mj::Value::object();
    props["package_name"] = propString("The package name of the app to close");

    ToolDef def;
    def.name = "close_app";
    def.description = "Close (force stop) an app on the cloud phone by its package name";
    def.inputSchema = makeSchema(props, {"package_name"});
    def.handler = [](const mj::Value& args) -> mj::Value {
        // 参数解析：package_name 为必填非空字符串
        std::string pkg, err;
        if (!getRequiredString(args, "package_name", pkg, err)) return errorResult(err);
        if (!provider().closeApp(pickBackend(args), pkg, err)) return errorResult(err);
        return textResult("Close app " + pkg + " successfully");
    };
    return def;
}

}  // namespace tool
