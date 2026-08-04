#include "base.hpp"

namespace tool {

ToolDef makeTapTool() {
    mj::Value props = mj::Value::object();
    props["x"] = propNumber("The x coordinate of the tap point");
    props["y"] = propNumber("The y coordinate of the tap point");

    ToolDef def;
    def.name = "tap";
    def.description = "Tap at specified coordinates on the cloud phone screen";
    def.inputSchema = makeSchema(props, {"x", "y"});
    def.handler = [](const mj::Value& args) -> mj::Value {
        int x = 0, y = 0;
        std::string err;
        if (!getInt(args, "x", x, err)) return errorResult(err);
        if (!getInt(args, "y", y, err)) return errorResult(err);
        if (!provider().tap(pickBackend(args), x, y, err)) return errorResult(err);
        return textResult("Tap the screen successfully at (" + std::to_string(x) + ", " +
                          std::to_string(y) + ")");
    };
    return def;
}

}  // namespace tool
