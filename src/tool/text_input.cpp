#include "base.hpp"

namespace tool {

ToolDef makeTextInputTool() {
    mj::Value props = mj::Value::object();
    props["text"] = propString("The text to input at the current focus");

    ToolDef def;
    def.name = "text_input";
    def.description =
        "Input text at the current focus on the cloud phone. ASCII text uses 'input text' "
        "directly; non-ASCII text (e.g. Chinese) is sent via the cloud phone input method "
        "broadcast and requires the matching IME to be installed";
    def.inputSchema = makeSchema(props, {"text"});
    def.handler = [](const mj::Value& args) -> mj::Value {
        std::string text, err;
        if (!getRequiredString(args, "text", text, err)) return errorResult(err);
        if (!provider().inputText(pickBackend(args), text, err)) return errorResult(err);
        return textResult("Input text successfully");
    };
    return def;
}

}  // namespace tool
