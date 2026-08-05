/**
 * @file provider.cpp
 * @brief MobileUseProvider 实现——CommandBuilder 拼命令 + 按 backend 路由执行
 *
 * 功能：
 *   实现 provider.hpp 声明的全部工具方法：每个方法用 cmd::xxx 生成
 *   shell 命令，经 executor(b) 路由到 LocalExecutor（adb）或
 *   CloudExecutor（云手机），再把 ExecResult 归约为 bool + err。
 *
 * 开发思路：
 *   1. "拼命令 -> 执行 -> 判 ok -> 失败拼 err"四段式模板贯穿所有方法，
 *      保持行为一致、错误文案统一（"xxx failed: ..."）；
 *   2. 少数方法有通道差异分支：screenshot 本地读 stdout 二进制、云端落盘；
 *      inputText 非 ASCII 走 IME+广播注入并有直输回退；
 *   3. listApps/installApp 需解析命令文本输出（"package:" 前缀行 /
 *      "Success" 关键字），把 shell 侧结果转成结构化数据。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "provider.hpp"

#include "../util/util.hpp"
#include "command_builder.hpp"

#include <cstdlib>
#include <cstring>  // strstr（screenshot 云端分支解析 "Physical size:"）

namespace service {

/**
 * @brief 字符串转 Backend
 * @param s 输入串（"cloud"/"adb"/"local"） @param def 无法识别时的默认值
 * @return 解析结果
 */
Backend backendFromString(const std::string& s, Backend def) {
    if (s == "cloud") return Backend::Cloud;
    if (s == "adb" || s == "local") return Backend::Adb;
    return def;
}

/** @brief Backend 转字符串 */
std::string backendToString(Backend b) {
    return b == Backend::Cloud ? "cloud" : "adb";
}

/** @brief 构造：保存默认后端并获取本地执行器单例（云端懒创建） */
MobileUseProvider::MobileUseProvider(Backend defaultBackend)
    : m_default(defaultBackend), m_local(createLocalExecutor()) {}

MobileUseProvider::~MobileUseProvider() = default;

/**
 * @brief 注入云端配置
 * @param config 云端配置；有效则立即创建云端执行器，无效仅存档待后续使用
 */
void MobileUseProvider::setCloudConfig(const CloudConfig& config) {
    m_cloudConfig = config;
    if (config.valid()) m_cloud = std::unique_ptr<Executor>(createCloudExecutor(config));
}

/**
 * @brief 从工具入参解析后端
 * @param args MCP 工具参数（可含 "backend" 字符串字段）
 * @return 参数指定的后端；未指定/无法识别回退默认后端
 * 伪代码：args 是对象且含 backend -> backendFromString -> 否则 m_default
 */
Backend MobileUseProvider::resolveBackend(const mj::Value& args) const {
    if (args.isObject() && args.has("backend"))
        return backendFromString(args["backend"].asString(), m_default);
    return m_default;
}

/**
 * @brief 按后端取执行器
 * @param b 目标后端
 * @return 执行器指针（云端首次访问时用已存配置懒创建）
 */
Executor* MobileUseProvider::executor(Backend b) {
    if (b == Backend::Cloud) {
        if (m_cloud) return m_cloud.get();
        m_cloud = std::unique_ptr<Executor>(createCloudExecutor(m_cloudConfig));
        return m_cloud.get();
    }
    return m_local;
}

/**
 * @brief 读取屏幕宽高
 * @param b 后端 @param width/height 输出宽高
 * @return 解析成功 true
 * 伪代码：runShell("wm size") -> 找 "Physical size:"/"Override size:" ->
 *         sscanf "%dx%d" -> 校验 >0
 */
bool MobileUseProvider::readScreenSize(Backend b, int& width, int& height) {
    ExecResult r = executor(b)->runShell(cmd::screenSize(), m_defaultTimeoutMs);
    if (!r.ok()) return false;
    size_t pos = r.out.find("Physical size:");
    if (pos == std::string::npos) {
        pos = r.out.find("Override size:");
        if (pos == std::string::npos) return false;
        pos += 14;
    } else {
        pos += 14;
    }
    if (sscanf(r.out.c_str() + pos, "%dx%d", &width, &height) != 2) return false;
    return width > 0 && height > 0;
}

/**
 * @brief 截屏
 * @param b 后端 @param shot 输出截图结果 @param err 失败描述
 * @return 成功 true
 * 实现思路：
 *   云端：screencap 落盘 /sdcard/.mcp_mobile_use_shot.png，同命令串带
 *         wm size 解析尺寸，最后 rm 临时文件；
 *   本地：screencap -p 直接 stdout 回读 PNG 二进制，校验 PNG 魔数后
 *         base64 编码，再单独 readScreenSize 补尺寸。
 */
bool MobileUseProvider::screenshot(Backend b, Screenshot& shot, std::string& err) {
    if (b == Backend::Cloud) {
        const std::string path = "/sdcard/.mcp_mobile_use_shot.png";
        ExecResult r = executor(b)->runShell(
            cmd::screenshotToFile(path) + " && " + cmd::screenSize() + " && " +
                cmd::removeFile(path),
            m_defaultTimeoutMs);
        if (!r.ok()) {
            err = "cloud screenshot failed: " + r.describe();
            return false;
        }
        shot.devicePath = path;
        int w = 0, h = 0;
        if (sscanf(strstr(r.out.c_str(), "Physical size:") ?
                           strstr(r.out.c_str(), "Physical size:") + 14 : "",
                       "%dx%d", &w, &h) == 2) {
            shot.width = w;
            shot.height = h;
        }
        return true;
    }

    // 本地通道：stdout 直接回读 PNG 二进制流
    ExecResult r = executor(b)->runShell(cmd::screenshotStdout(), m_defaultTimeoutMs);
    if (!r.ok()) {
        err = "screenshot failed: " + r.describe();
        return false;
    }
    // 校验 PNG 文件头魔数，防止把错误文本当图片
    const std::string kPngMagic = "\x89PNG";
    if (r.out.size() < 8 || r.out.compare(0, 4, kPngMagic) != 0) {
        err = "screenshot output is not png: " + r.out.substr(0, 128);
        return false;
    }
    shot.base64Png = util::base64Encode(r.out);
    readScreenSize(b, shot.width, shot.height);
    return true;
}

/** @brief 点击：拼 input tap 命令执行，失败拼 "tap failed: ..." */
bool MobileUseProvider::tap(Backend b, int x, int y, std::string& err) {
    ExecResult r = executor(b)->runShell(cmd::tap(x, y), m_defaultTimeoutMs);
    if (!r.ok()) {
        err = "tap failed: " + r.describe();
        return false;
    }
    return true;
}

/**
 * @brief 滑动（超时 = 默认超时 + 滑动耗时，避免长滑动被误判超时）
 */
bool MobileUseProvider::swipe(Backend b, int x1, int y1, int x2, int y2, int durationMs,
                              std::string& err) {
    ExecResult r = executor(b)->runShell(cmd::swipe(x1, y1, x2, y2, durationMs),
                                         m_defaultTimeoutMs + durationMs);
    if (!r.ok()) {
        err = "swipe failed: " + r.describe();
        return false;
    }
    return true;
}

/**
 * @brief 输入文本
 * @param text 待输入内容
 * @return 成功 true
 * 实现思路：
 *   1. 纯 ASCII 可见字符：直接 input text 直输（快且无需切 IME）；
 *   2. 非 ASCII：先切拼音 IME、广播清空输入框，再广播注入文本；
 *      广播失败时回退 input text 直输再试一次；
 *   3. 全部失败时把两条路径的错误都拼进 err 便于诊断。
 */
bool MobileUseProvider::inputText(Backend b, const std::string& text, std::string& err) {
    if (cmd::isAsciiPrintable(text)) {
        ExecResult r = executor(b)->runShell(cmd::inputTextDirect(text), m_defaultTimeoutMs);
        if (r.ok()) return true;
        err = "input text failed: " + r.describe();
        return false;
    }
    ExecResult ime = executor(b)->runShell(cmd::textInputSelectIme(), m_defaultTimeoutMs);
    (void)ime;
    ExecResult clear = executor(b)->runShell(cmd::textInputClear(), m_defaultTimeoutMs);
    (void)clear;
    ExecResult r = executor(b)->runShell(cmd::textInputBroadcast(text), m_defaultTimeoutMs);
    if (r.ok()) return true;
    ExecResult r2 = executor(b)->runShell(cmd::inputTextDirect(text), m_defaultTimeoutMs);
    if (r2.ok()) return true;
    err = "input text failed: broadcast: " + r.describe() + "; fallback: " + r2.describe();
    return false;
}

/** @brief 按键：拼 input keyevent 命令执行 */
bool MobileUseProvider::keyEvent(Backend b, int keyCode, std::string& err) {
    ExecResult r = executor(b)->runShell(cmd::keyEvent(keyCode), m_defaultTimeoutMs);
    if (!r.ok()) {
        err = "key event failed: " + r.describe();
        return false;
    }
    return true;
}

/** @brief 启动应用：monkey + am start 双保险命令（见 CommandBuilder） */
bool MobileUseProvider::launchApp(Backend b, const std::string& packageName, std::string& err) {
    ExecResult r = executor(b)->runShell(cmd::launchApp(packageName), m_defaultTimeoutMs);
    if (!r.ok()) {
        err = "launch app failed: " + r.describe();
        return false;
    }
    return true;
}

/** @brief 关闭应用：am force-stop */
bool MobileUseProvider::closeApp(Backend b, const std::string& packageName, std::string& err) {
    ExecResult r = executor(b)->runShell(cmd::closeApp(packageName), m_defaultTimeoutMs);
    if (!r.ok()) {
        err = "close app failed: " + r.describe();
        return false;
    }
    return true;
}

/**
 * @brief 列出应用包名
 * @param thirdPartyOnly 仅第三方应用 @param apps 输出列表 @param err 失败描述
 * @return 成功 true
 * 伪代码：runShell(pm list packages [-3]) -> 逐行扫描 ->
 *         以 "package:" 开头的行截取包名入列
 */
bool MobileUseProvider::listApps(Backend b, bool thirdPartyOnly, std::vector<AppItem>& apps,
                                 std::string& err) {
    ExecResult r = executor(b)->runShell(cmd::listPackages(thirdPartyOnly), m_defaultTimeoutMs);
    if (!r.ok()) {
        err = "list apps failed: " + r.describe();
        return false;
    }
    size_t pos = 0;
    while (pos < r.out.size()) {
        size_t end = r.out.find('\n', pos);
        if (end == std::string::npos) end = r.out.size();
        std::string line = util::trim(r.out.substr(pos, end - pos));
        pos = end + 1;
        const std::string prefix = "package:";
        if (util::startsWith(line, prefix)) {
            AppItem item;
            item.packageName = line.substr(prefix.size());
            if (!item.packageName.empty()) apps.push_back(item);
        }
    }
    return true;
}

/**
 * @brief 通用入口：透传执行任意 shell 命令（对齐 adb shell <cmd>）
 * @param command 原始命令串 @param timeoutMs 超时毫秒（<=0 用默认）
 * @param out 完整 ExecResult（含 stdout/stderr/exitCode） @param err 失败描述
 * @return 仅框架级错误（网络/管道等）返回 false；命令非零退出仍返回 true，
 *         由调用方查看 out.exitCode
 */
bool MobileUseProvider::runShell(Backend b, const std::string& command, int timeoutMs,
                                 ExecResult& out, std::string& err) {
    out = executor(b)->runShell(command, timeoutMs > 0 ? timeoutMs : m_defaultTimeoutMs);
    if (!out.error.empty()) {
        err = "adb shell failed: " + out.error;
        return false;
    }
    return true;
}

/**
 * @brief 下载并安装 APK
 * @param downloadUrl APK 下载地址 @param err 失败描述
 * @return 成功 true
 * 实现思路：用 installApk 复合命令（curl/wget 下载 + pm install + 清理），
 *   超时放宽到 300s；pm install 退出码为 0 不代表成功，还需在输出中
 *   找 "Success"/"success" 关键字二次确认。
 */
bool MobileUseProvider::installApp(Backend b, const std::string& downloadUrl,
                                   std::string& err) {
    const std::string localPath = "/data/local/tmp/mcp_mobile_use_install.apk";
    ExecResult r = executor(b)->runShell(cmd::installApk(downloadUrl, localPath), 300000);
    if (!r.ok()) {
        err = "install app failed: " + r.describe();
        return false;
    }
    if (r.out.find("Success") == std::string::npos && r.out.find("success") == std::string::npos) {
        err = "install app failed: " + util::trim(r.out);
        return false;
    }
    return true;
}

}  // namespace service
