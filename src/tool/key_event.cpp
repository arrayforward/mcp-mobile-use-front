/**
 * @file key_event.cpp
 * @brief back/home/menu 三个按键事件工具实现——向云手机发送系统按键
 *
 * 功能：
 *   定义 MCP 工具 "back"（键码 4）、"home"（键码 3）、"menu"（键码 82），
 *   经全局 provider 向设备发送对应按键事件（adb 后端为 input keyevent）。
 *
 * 开发思路：
 *   三个工具结构完全一致，仅名称/描述/键码/成功提示不同，故抽取
 *   匿名命名空间内的 makeKeyEventTool() 公共工厂，按键码捕获进
 *   lambda，避免三份重复代码。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "base.hpp"

namespace tool {

namespace {

/**
 * @brief 按键事件工具公共工厂
 * @param name       工具名（back/home/menu）
 * @param desc       工具描述
 * @param keyCode    Android 键码（BACK=4, HOME=3, MENU=82）
 * @param successMsg 成功时返回的提示文本
 * @return 对应的 ToolDef
 *
 * 伪代码：构造空属性 schema（仅含自动注入的 backend）-> handler 捕获
 *   keyCode/successMsg -> 调用 provider().keyEvent -> 返回结果。
 */
ToolDef makeKeyEventTool(const std::string& name, const std::string& desc, int keyCode,
                         const std::string& successMsg) {
    mj::Value props = mj::Value::object();

    ToolDef def;
    def.name = name;
    def.description = desc;
    def.inputSchema = makeSchema(props, {});
    // 按值捕获键码与提示语，生成无参（仅 backend）工具的 handler
    def.handler = [keyCode, successMsg](const mj::Value& args) -> mj::Value {
        std::string err;
        if (!provider().keyEvent(pickBackend(args), keyCode, err)) return errorResult(err);
        return textResult(successMsg);
    };
    return def;
}

}  // namespace

/**
 * @brief 构造 back 工具的 ToolDef（返回键，键码 4）
 * @return 名为 "back" 的工具定义
 */
ToolDef makeKeyEventBackTool() {
    return makeKeyEventTool("back", "Send the BACK key event to the cloud phone", 4,
                            "Send back key event successfully");
}

/**
 * @brief 构造 home 工具的 ToolDef（主页键，键码 3）
 * @return 名为 "home" 的工具定义
 */
ToolDef makeKeyEventHomeTool() {
    return makeKeyEventTool("home", "Send the HOME key event to the cloud phone", 3,
                            "Send home key event successfully");
}

/**
 * @brief 构造 menu 工具的 ToolDef（菜单键，键码 82）
 * @return 名为 "menu" 的工具定义
 */
ToolDef makeKeyEventMenuTool() {
    return makeKeyEventTool("menu", "Send the MENU key event to the cloud phone", 82,
                            "Send menu key event successfully");
}

}  // namespace tool
