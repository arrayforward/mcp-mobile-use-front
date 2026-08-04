#include "base.hpp"

namespace tool {

ToolDef makeTerminateTool() {
    mj::Value props = mj::Value::object();

    ToolDef def;
    def.name = "terminate";
    def.description = "Terminate the current mobile use session";
    def.inputSchema = makeSchema(props, {});
    def.handler = [](const mj::Value&) -> mj::Value {
        return textResult("Session terminated");
    };
    return def;
}

}  // namespace tool
