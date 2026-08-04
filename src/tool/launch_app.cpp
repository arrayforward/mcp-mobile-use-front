#include "base.hpp"

namespace tool {

ToolDef makeLaunchAppTool() {
    mj::Value props = mj::Value::object();
    props["package_name"] = propString("The package name of the app to launch");

    ToolDef def;
    def.name = "launch_app";
    def.description = "Launch an app on the cloud phone by its package name";
    def.inputSchema = makeSchema(props, {"package_name"});
    def.handler = [](const mj::Value& args) -> mj::Value {
        std::string pkg, err;
        if (!getRequiredString(args, "package_name", pkg, err)) return errorResult(err);
        if (!provider().launchApp(pickBackend(args), pkg, err)) return errorResult(err);
        return textResult("Launch app " + pkg + " successfully");
    };
    return def;
}

}  // namespace tool
