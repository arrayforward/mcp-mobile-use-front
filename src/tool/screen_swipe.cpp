#include "base.hpp"

namespace tool {

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
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        std::string err;
        if (!getInt(args, "from_x", x1, err)) return errorResult(err);
        if (!getInt(args, "from_y", y1, err)) return errorResult(err);
        if (!getInt(args, "to_x", x2, err)) return errorResult(err);
        if (!getInt(args, "to_y", y2, err)) return errorResult(err);
        int duration = 300;
        if (args.has("duration_ms") && args["duration_ms"].isNumber())
            duration = args["duration_ms"].asInt(300);
        if (!provider().swipe(pickBackend(args), x1, y1, x2, y2, duration, err))
            return errorResult(err);
        return textResult("Swipe the screen successfully");
    };
    return def;
}

}  // namespace tool
