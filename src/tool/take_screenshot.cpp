#include "base.hpp"

namespace tool {

ToolDef makeTakeScreenshotTool() {
    mj::Value props = mj::Value::object();

    ToolDef def;
    def.name = "take_screenshot";
    def.description =
        "Take a screenshot of the cloud phone screen. With the default 'adb' backend the "
        "png image is returned inline as base64 image content. With the 'cloud' backend the "
        "screenshot is saved on the device and the file path is returned, because the cloud "
        "API limits command output size.";
    def.inputSchema = makeSchema(props, {});
    def.handler = [](const mj::Value& args) -> mj::Value {
        service::Backend backend = pickBackend(args);
        service::MobileUseProvider::Screenshot shot;
        std::string err;
        if (!provider().screenshot(backend, shot, err)) return errorResult(err);
        if (backend == service::Backend::Cloud) {
            mj::Value info = mj::Value::object();
            info["device_path"] = shot.devicePath;
            info["width"] = shot.width;
            info["height"] = shot.height;
            return textResult(info.dump());
        }
        return imageResult(shot.base64Png);
    };
    return def;
}

}  // namespace tool
