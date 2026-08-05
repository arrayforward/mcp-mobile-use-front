/**
 * @file screen_swipe.cpp
 * @brief swipe 工具实现——在云手机屏幕上执行两点间滑动
 *
 * 功能：
 *   定义 MCP 工具 "swipe"：接收起点/终点坐标与可选滑动时长，
 *   经全局 provider 在设备上执行滑动手势（adb 后端为 input swipe）。
 *
 * 开发思路：
 *   起点/终点四坐标为必填，duration_ms 为可选参数（默认 300ms）；
 *   handler 内先解析必填参数，再用 has()+asInt 容错读取可选参数。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "base.hpp"

namespace tool {

/**
 * @brief 构造 swipe 工具的 ToolDef
 * @return 名为 "swipe" 的工具定义（含 inputSchema 与 handler）
 *
 * 伪代码：
 *   1. 定义属性 from_x/from_y/to_x/to_y（必填）与 duration_ms（可选，默认 300）；
 *   2. handler：解析四坐标 -> 读取可选时长 -> pickBackend ->
 *      provider().swipe -> 成功返回文本结果，失败返回 errorResult。
 */
ToolDef makeSwipeTool() {
    mj::Value props = mj::Value::object();
    props["from_x"] = propNumber("The x coordinate of the swipe start point");
    props["from_y"] = propNumber("The y coordinate of the swipe start point");
    props["to_x"] = propNumber("The x coordinate of the swipe end point");
    props["to_y"] = propNumber("The y coordinate of the swipe end point");
    props["duration_ms"] = propNumber("Swipe duration in milliseconds, default 300");

    ToolDef def;
    def.name = "swipe";
    def.description = "Swipe on the cloud phone screen from one point to another";
    def.inputSchema = makeSchema(props, {"from_x", "from_y", "to_x", "to_y"});
    def.handler = [](const mj::Value& args) -> mj::Value {
        // 参数解析：起点/终点四坐标均为必填整数
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        std::string err;
        if (!getInt(args, "from_x", x1, err)) return errorResult(err);
        if (!getInt(args, "from_y", y1, err)) return errorResult(err);
        if (!getInt(args, "to_x", x2, err)) return errorResult(err);
        if (!getInt(args, "to_y", y2, err)) return errorResult(err);
        // 可选参数：滑动时长，缺省 300 毫秒
        int duration = 300;
        if (args.has("duration_ms") && args["duration_ms"].isNumber())
            duration = args["duration_ms"].asInt(300);
        // 选择后端并执行滑动
        if (!provider().swipe(pickBackend(args), x1, y1, x2, y2, duration, err))
            return errorResult(err);
        return textResult("Swipe the screen successfully");
    };
    return def;
}

}  // namespace tool
