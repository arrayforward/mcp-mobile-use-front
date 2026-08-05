/**
 * @file screen_tap.cpp
 * @brief tap 工具实现——在云手机屏幕指定坐标执行点击
 *
 * 功能：
 *   定义 MCP 工具 "tap"：接收 x/y 坐标参数，经全局 provider 在设备上
 *   执行点击操作（adb 后端为 input tap，cloud 后端走 CPH API）。
 *
 * 开发思路：
 *   遵循工具工厂模式：构造 schema（x/y 必填）-> 组装 ToolDef ->
 *   handler 内做参数解析 -> 后端选择 -> 调用 provider -> 返回文本结果。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "base.hpp"

namespace tool {

/**
 * @brief 构造 tap 工具的 ToolDef
 * @return 名为 "tap" 的工具定义（含 inputSchema 与 handler）
 *
 * 伪代码：
 *   1. 定义属性 x/y（number，必填）；
 *   2. makeSchema 生成 schema（自动含 backend 属性）；
 *   3. handler：解析 x/y -> pickBackend -> provider().tap ->
 *      失败返回 errorResult，成功返回含坐标的文本结果。
 */
ToolDef makeTapTool() {
    mj::Value props = mj::Value::object();
    props["x"] = propNumber("The x coordinate of the tap point");
    props["y"] = propNumber("The y coordinate of the tap point");

    ToolDef def;
    def.name = "tap";
    def.description = "Tap at specified coordinates on the cloud phone screen";
    def.inputSchema = makeSchema(props, {"x", "y"});
    def.handler = [](const mj::Value& args) -> mj::Value {
        // 参数解析：x/y 为必填整数，缺失或类型错误直接返回错误结果
        int x = 0, y = 0;
        std::string err;
        if (!getInt(args, "x", x, err)) return errorResult(err);
        if (!getInt(args, "y", y, err)) return errorResult(err);
        // 按参数选择后端并执行点击，失败时 err 携带底层原因
        if (!provider().tap(pickBackend(args), x, y, err)) return errorResult(err);
        return textResult("Tap the screen successfully at (" + std::to_string(x) + ", " +
                          std::to_string(y) + ")");
    };
    return def;
}

}  // namespace tool
