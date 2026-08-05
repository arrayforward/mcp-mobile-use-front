/**
 * @file registry.cpp
 * @brief 工具注册中心——汇总全部 MCP 工具的工厂函数
 *
 * 功能：
 *   前置声明各工具实现文件中的 makeXxxTool() 工厂函数，
 *   在 allTools() 中按固定顺序实例化并返回全部 ToolDef，
 *   供 MCP 服务层响应 tools/list 请求。
 *
 * 开发思路：
 *   采用"一个工具一个文件 + 集中注册"模式：新增工具只需
 *   新建 .cpp 实现工厂函数，并在此处加一行前置声明与 push_back，
 *   服务层代码零改动；返回顺序即 tools/list 的展示顺序。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "base.hpp"

namespace tool {

// 各工具工厂函数的前置声明（实现分散于对应 .cpp 文件）
ToolDef makeTapTool();             // tap：屏幕坐标点击
ToolDef makeAdbShellTool();        // adb_shell：通用 adb shell 命令执行
ToolDef makeSwipeTool();           // swipe：屏幕滑动
ToolDef makeTakeScreenshotTool();  // take_screenshot：屏幕截图
ToolDef makeTextInputTool();       // text_input：文本输入
ToolDef makeKeyEventBackTool();    // back：返回键事件
ToolDef makeKeyEventHomeTool();    // home：主页键事件
ToolDef makeKeyEventMenuTool();    // menu：菜单键事件
ToolDef makeLaunchAppTool();       // launch_app：按包名启动应用
ToolDef makeCloseAppTool();        // close_app：按包名强制停止应用
ToolDef makeListAppTool();         // list_apps：列出已安装应用
ToolDef makeInstallAppTool();      // autoinstall_app：下载并安装 APK
ToolDef makeTerminateTool();       // terminate：终止当前会话

/**
 * @brief 汇总全部已注册工具
 * @return 按注册顺序排列的 ToolDef 列表（即 tools/list 的返回顺序）
 *
 * 伪代码：依次调用各 makeXxxTool() 工厂 -> push_back 收集 -> 返回
 */
std::vector<ToolDef> allTools() {
    std::vector<ToolDef> tools;
    tools.push_back(makeTapTool());
    tools.push_back(makeAdbShellTool());
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
