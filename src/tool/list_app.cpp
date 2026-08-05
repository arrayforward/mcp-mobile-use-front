/**
 * @file list_app.cpp
 * @brief list_apps 工具实现——列出云手机上已安装的应用
 *
 * 功能：
 *   定义 MCP 工具 "list_apps"：默认仅列出第三方应用，
 *   可选参数 include_system=true 时包含系统应用；
 *   结果以 JSON 文本形式返回 {apps:[{package_name,app_status}],count}。
 *
 * 开发思路：
 *   provider().listApps 的第三个参数语义为 onlyThirdParty（仅第三方），
 *   与对外参数 include_system 相反，调用时取反转换；
 *   输出结构对齐参考实现 mobile_use api（app_status 固定 "deployed"）。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "base.hpp"

namespace tool {

/**
 * @brief 构造 list_apps 工具的 ToolDef
 * @return 名为 "list_apps" 的工具定义（含 inputSchema 与 handler）
 *
 * 伪代码：
 *   handler：读可选 include_system（默认 false）-> 取反得 onlyThirdParty ->
 *   provider().listApps -> 逐条组装 {package_name,app_status} 数组 ->
 *   附 count 后 dump 为文本返回。
 */
ToolDef makeListAppTool() {
    mj::Value props = mj::Value::object();
    props["include_system"] =
        propBool("Whether to include system apps, default false (third party apps only)");

    ToolDef def;
    def.name = "list_apps";
    def.description = "List installed apps on the cloud phone";
    def.inputSchema = makeSchema(props, {});
    def.handler = [](const mj::Value& args) -> mj::Value {
        // 可选参数：是否包含系统应用，默认仅第三方
        bool includeSystem = args.has("include_system") && args["include_system"].asBool(false);
        std::vector<service::MobileUseProvider::AppItem> apps;
        std::string err;
        // 注意语义取反：provider 接口参数为 onlyThirdParty
        if (!provider().listApps(pickBackend(args), !includeSystem, apps, err))
            return errorResult(err);
        // 组装应用列表 JSON：包名 + 状态（对齐参考 api 固定为 deployed）
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
