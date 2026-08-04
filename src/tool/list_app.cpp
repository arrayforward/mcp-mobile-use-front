#include "base.hpp"

namespace tool {

ToolDef makeListAppTool() {
    mj::Value props = mj::Value::object();
    props["include_system"] =
        propBool("Whether to include system apps, default false (third party apps only)");

    ToolDef def;
    def.name = "list_apps";
    def.description = "List installed apps on the cloud phone";
    def.inputSchema = makeSchema(props, {});
    def.handler = [](const mj::Value& args) -> mj::Value {
        bool includeSystem = args.has("include_system") && args["include_system"].asBool(false);
        std::vector<service::MobileUseProvider::AppItem> apps;
        std::string err;
        if (!provider().listApps(pickBackend(args), !includeSystem, apps, err))
            return errorResult(err);
        mj::Value result = mj::Value::object();
        mj::Value arr = mj::Value::array();
        for (const auto& app : apps) {
            mj::Value item = mj::Value::object();
            item["package_name"] = app.packageName;
            item["app_status"] = "deployed";
            arr.push(item);
        }
        result["apps"] = arr;
        result["count"] = static_cast<long long>(apps.size());
        return textResult(result.dump());
    };
    return def;
}

}  // namespace tool
