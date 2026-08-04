#include "base.hpp"

namespace tool {

ToolDef makeTapTool();
ToolDef makeSwipeTool();
ToolDef makeTakeScreenshotTool();
ToolDef makeTextInputTool();
ToolDef makeKeyEventBackTool();
ToolDef makeKeyEventHomeTool();
ToolDef makeKeyEventMenuTool();
ToolDef makeLaunchAppTool();
ToolDef makeCloseAppTool();
ToolDef makeListAppTool();
ToolDef makeInstallAppTool();
ToolDef makeTerminateTool();

std::vector<ToolDef> allTools() {
    std::vector<ToolDef> tools;
    tools.push_back(makeTapTool());
    tools.push_back(makeSwipeTool());
    tools.push_back(makeTakeScreenshotTool());
    tools.push_back(makeTextInputTool());
    tools.push_back(makeKeyEventBackTool());
    tools.push_back(makeKeyEventHomeTool());
    tools.push_back(makeKeyEventMenuTool());
    tools.push_back(makeLaunchAppTool());
    tools.push_back(makeCloseAppTool());
    tools.push_back(makeListAppTool());
    tools.push_back(makeInstallAppTool());
    tools.push_back(makeTerminateTool());
    return tools;
}

}  // namespace tool
