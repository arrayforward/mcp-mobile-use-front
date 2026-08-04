#include "base.hpp"

namespace tool {

ToolDef makeInstallAppTool() {
    mj::Value props = mj::Value::object();
    props["download_url"] = propString("The http(s) download url of the apk to install");
    props["app_name"] = propString("The app name to be installed (optional, kept for "
                                   "compatibility with the reference mobile_use api)");

    ToolDef def;
    def.name = "autoinstall_app";
    def.description =
        "Download and install an app in one step on the cloud phone. Requires "
        "curl or wget on the device";
    def.inputSchema = makeSchema(props, {"download_url"});
    def.handler = [](const mj::Value& args) -> mj::Value {
        std::string url, err;
        if (!getRequiredString(args, "download_url", url, err)) return errorResult(err);
        if (url.find("http://") != 0 && url.find("https://") != 0)
            return errorResult("download_url is invalid: " + url);
        if (!provider().installApp(pickBackend(args), url, err)) return errorResult(err);
        return textResult("Apk is installed successfully");
    };
    return def;
}

}  // namespace tool
