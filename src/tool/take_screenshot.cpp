/**
 * @file take_screenshot.cpp
 * @brief take_screenshot 工具实现——截取云手机屏幕画面
 *
 * 功能：
 *   定义 MCP 工具 "take_screenshot"：截取设备当前屏幕。
 *   adb 后端直接返回 base64 内联 PNG 图片；cloud 后端因 API 输出
 *   大小限制，将截图保存在设备上并返回文件路径与分辨率信息。
 *
 * 开发思路：
 *   无业务参数（仅自动注入的 backend）；handler 根据后端类型
 *   分支返回不同形式结果，保证两种后端下的信息完整性。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "base.hpp"

namespace tool {

/**
 * @brief 构造 take_screenshot 工具的 ToolDef
 * @return 名为 "take_screenshot" 的工具定义（含 inputSchema 与 handler）
 *
 * 伪代码：
 *   handler：pickBackend -> provider().screenshot 取截图 ->
 *     cloud 后端：返回 {device_path,width,height} 的 JSON 文本；
 *     adb 后端：返回 base64 PNG 图片内容（imageResult）。
 */
ToolDef makeTakeScreenshotTool() {
    mj::Value props = mj::Value::object();

    ToolDef def;
    def.name = "take_screenshot";
    def.description =
        "Take a screenshot of the cloud phone screen. With the default 'adb' backend the "
        "png image is returned inline as base64 image content. With the 'cloud' backend the "
        "screenshot is saved on the device and the file path is returned, because the cloud "
        "API limits command output size.";
    def.inputSchema = makeSchema(props, {});
    def.handler = [](const mj::Value& args) -> mj::Value {
        service::Backend backend = pickBackend(args);
        service::MobileUseProvider::Screenshot shot;
        std::string err;
        if (!provider().screenshot(backend, shot, err)) return errorResult(err);
        // cloud 后端：云端 API 限制命令输出大小，截图仅存于设备，返回路径与尺寸
        if (backend == service::Backend::Cloud) {
            mj::Value info = mj::Value::object();
            info["device_path"] = shot.devicePath;
            info["width"] = shot.width;
            info["height"] = shot.height;
            return textResult(info.dump());
        }
        // adb 后端：直接内联返回 base64 PNG 图片
        return imageResult(shot.base64Png);
    };
    return def;
}

}  // namespace tool
