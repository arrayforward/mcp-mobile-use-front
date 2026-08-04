#include "base.hpp"

namespace tool {

namespace {

ToolDef makeKeyEventTool(const std::string& name, const std::string& desc, int keyCode,
                         const std::string& successMsg) {
    mj::Value props = mj::Value::object();

    ToolDef def;
    def.name = name;
    def.description = desc;
    def.inputSchema = makeSchema(props, {});
    def.handler = [keyCode, successMsg](const mj::Value& args) -> mj::Value {
        std::string err;
        if (!provider().keyEvent(pickBackend(args), keyCode, err)) return errorResult(err);
        return textResult(successMsg);
    };
    return def;
}

}  // namespace

ToolDef makeKeyEventBackTool() {
    return makeKeyEventTool("back", "Send the BACK key event to the cloud phone", 4,
                            "Send back key event successfully");
}

ToolDef makeKeyEventHomeTool() {
    return makeKeyEventTool("home", "Send the HOME key event to the cloud phone", 3,
                            "Send home key event successfully");
}

ToolDef makeKeyEventMenuTool() {
    return makeKeyEventTool("menu", "Send the MENU key event to the cloud phone", 82,
                            "Send menu key event successfully");
}

}  // namespace tool
