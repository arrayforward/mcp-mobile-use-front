#include "base.hpp"

namespace tool {

ToolDef makeCloseAppTool() {
    mj::Value props = mj::Value::object();
    props["package_name"] = propString("The package name of the app to close");

    ToolDef def;
    def.name = "close_app";
    def.description = "Close (force stop) an app on the cloud phone by its package name";
    def.inputSchema = makeSchema(props, {"package_name"});
    def.handler = [](const mj::Value& args) -> mj::Value {
        std::string pkg, err;
        if (!getRequiredString(args, "package_name", pkg, err)) return errorResult(err);
        if (!provider().closeApp(pickBackend(args), pkg, err)) return errorResult(err);
        return textResult("Close app " + pkg + " successfully");
    };
    return def;
}

}  // namespace tool
