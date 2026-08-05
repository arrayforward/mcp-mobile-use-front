/**
 * @file install_app.cpp
 * @brief autoinstall_app 工具实现——从 URL 下载并一步安装 APK
 *
 * 功能：
 *   定义 MCP 工具 "autoinstall_app"：接收 download_url 参数，
 *   在云手机上下载对应 APK 并完成安装（依赖设备上的 curl 或 wget）。
 *
 * 开发思路：
 *   工具层先校验 URL 必须以 http:// 或 https:// 开头，拦截明显
 *   非法输入，避免将垃圾参数传给底层 shell 命令拼接；
 *   app_name 仅为兼容参考 mobile_use api 而保留，不参与实际逻辑。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "base.hpp"

namespace tool {

/**
 * @brief 构造 autoinstall_app 工具的 ToolDef
 * @return 名为 "autoinstall_app" 的工具定义（含 inputSchema 与 handler）
 *
 * 伪代码：
 *   handler：解析必填 download_url -> 校验 http/https 前缀 ->
 *   pickBackend -> provider().installApp（下载+安装）-> 返回结果。
 */
ToolDef makeInstallAppTool() {
    mj::Value props = mj::Value::object();
    props["download_url"] = propString("The http(s) download url of the apk to install");
    props["app_name"] = propString("The app name to be installed (optional, kept for "
                                   "compatibility with the reference mobile_use api)");

    ToolDef def;
    def.name = "autoinstall_app";
    def.description =
        "Download and install an app in one step on the cloud phone. Requires "
        "curl or wget on the device";
    def.inputSchema = makeSchema(props, {"download_url"});
    def.handler = [](const mj::Value& args) -> mj::Value {
        // 参数解析：download_url 为必填非空字符串
        std::string url, err;
        if (!getRequiredString(args, "download_url", url, err)) return errorResult(err);
        // URL 前缀校验：仅允许 http/https，防止非法协议注入 shell 命令
        if (url.find("http://") != 0 && url.find("https://") != 0)
            return errorResult("download_url is invalid: " + url);
        if (!provider().installApp(pickBackend(args), url, err)) return errorResult(err);
        return textResult("Apk is installed successfully");
    };
    return def;
}

}  // namespace tool
