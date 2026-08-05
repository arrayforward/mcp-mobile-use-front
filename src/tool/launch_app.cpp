/**
 * @file launch_app.cpp
 * @brief launch_app 工具实现——按包名在云手机上启动应用
 *
 * 功能：
 *   定义 MCP 工具 "launch_app"：接收 package_name 参数，
 *   经全局 provider 启动对应应用（adb 后端为 monkey/am start 命令）。
 *
 * 开发思路：
 *   单一必填字符串参数；工具层只做参数校验与结果包装，
 *   具体启动命令的拼接封装在 provider().launchApp 中。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "base.hpp"

namespace tool {

/**
 * @brief 构造 launch_app 工具的 ToolDef
 * @return 名为 "launch_app" 的工具定义（含 inputSchema 与 handler）
 *
 * 伪代码：
 *   handler：解析必填 package_name -> pickBackend ->
 *   provider().launchApp -> 成功返回含包名的文本，失败返回 errorResult。
 */
ToolDef makeLaunchAppTool() {
    mj::Value props = mj::Value::object();
    props["package_name"] = propString("The package name of the app to launch");

    ToolDef def;
    def.name = "launch_app";
    def.description = "Launch an app on the cloud phone by its package name";
    def.inputSchema = makeSchema(props, {"package_name"});
    def.handler = [](const mj::Value& args) -> mj::Value {
        // 参数解析：package_name 为必填非空字符串
        std::string pkg, err;
        if (!getRequiredString(args, "package_name", pkg, err)) return errorResult(err);
        if (!provider().launchApp(pickBackend(args), pkg, err)) return errorResult(err);
        return textResult("Launch app " + pkg + " successfully");
    };
    return def;
}

}  // namespace tool
