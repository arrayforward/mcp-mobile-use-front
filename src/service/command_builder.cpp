/**
 * @file command_builder.cpp
 * @brief 12 个 MCP 工具的 Android shell 命令模板实现
 *
 * 功能：
 *   实现 command_builder.hpp 声明的全部命令生成函数，输出可直接交给
 *   Executor::runShell 执行的 shell 命令串。
 *
 * 开发思路：
 *   1. 简单命令（tap/swipe/keyevent/screencap 等）直接字符串拼接；
 *      含外部输入的一律 util::shellQuote 包裹，杜绝注入。
 *   2. 复杂命令（launchApp / installApk）拼成带容错回退的复合 shell：
 *      launchApp 先 monkey 再 am start 精确解析回退 monkey；
 *      installApk 先 curl 失败后 wget，下载失败清理残文件，安装后必删 apk。
 *   3. inputTextDirect 需按 `input text` 的转义规则处理：空格 -> %s、% -> %25。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "command_builder.hpp"

#include "../util/util.hpp"

namespace service {
namespace cmd {

/**
 * @brief 生成点击命令
 * @param x 横坐标（像素） @param y 纵坐标（像素）
 * @return "input tap x y"
 * 伪代码：拼接 "input tap " + x + " " + y
 */
std::string tap(int x, int y) {
    return "input tap " + std::to_string(x) + " " + std::to_string(y);
}

/**
 * @brief 生成滑动命令
 * @param x1,y1 起点坐标 @param x2,y2 终点坐标 @param durationMs 滑动耗时（毫秒）
 * @return "input swipe x1 y1 x2 y2 durationMs"
 */
std::string swipe(int x1, int y1, int x2, int y2, int durationMs) {
    return "input swipe " + std::to_string(x1) + " " + std::to_string(y1) + " " +
           std::to_string(x2) + " " + std::to_string(y2) + " " + std::to_string(durationMs);
}

/**
 * @brief 生成按键命令
 * @param keyCode Android KeyEvent 键码（如 4=BACK）
 * @return "input keyevent keyCode"
 */
std::string keyEvent(int keyCode) {
    return "input keyevent " + std::to_string(keyCode);
}

/**
 * @brief 截图输出到 stdout（本地 adb 通道可直接回读二进制 PNG）
 * @return "screencap -p"
 */
std::string screenshotStdout() {
    return "screencap -p";
}

/**
 * @brief 截图保存到设备文件（云端通道无法回传二进制，先落盘再走别的取图途径）
 * @param path 设备侧目标路径
 * @return "screencap -p <path>"（路径已 shellQuote）
 */
std::string screenshotToFile(const std::string& path) {
    return "screencap -p " + util::shellQuote(path);
}

/**
 * @brief 删除设备文件
 * @param path 设备侧文件路径
 * @return "rm -f <path>"（-f 忽略不存在错误，路径已 shellQuote）
 */
std::string removeFile(const std::string& path) {
    return "rm -f " + util::shellQuote(path);
}

/**
 * @brief 查询屏幕物理尺寸
 * @return "wm size"，输出形如 "Physical size: 1080x2400"
 */
std::string screenSize() {
    return "wm size";
}

/**
 * @brief 切换默认输入法为拼音 IME（云手机广播注入文本的前置步骤）
 * @return settings put secure default_input_method 命令
 */
std::string textInputSelectIme() {
    return "settings put secure default_input_method "
           "'com.android.inputmethod.pinyin/.PinyinIME'";
}

/**
 * @brief 发送清空输入框广播（gameservice 自定义 action）
 * @return am broadcast 清屏命令
 */
std::string textInputClear() {
    return "am broadcast -a device.gameservice.keyevent.clear";
}

/**
 * @brief 通过广播把文本注入当前焦点输入框（支持中文等任意 UTF-8 文本）
 * @param text 待输入文本
 * @return am broadcast --es value <text> 命令（文本已 shellQuote）
 * 伪代码：拼接 action=device.gameservice.keyevent.value 与 extra value
 */
std::string textInputBroadcast(const std::string& text) {
    return "am broadcast -a device.gameservice.keyevent.value --es value " +
           util::shellQuote(text);
}

/**
 * @brief 生成 `input text` 直输命令（仅适合 ASCII 可见字符）
 * @param text 待输入文本
 * @return "input text <escaped>" 命令
 * 实现思路：`input text` 对空格与 % 有特殊语义，需按规则预转义：
 *   空格 -> "%s"，% -> "%25"；其余字符原样保留，最后整体 shellQuote。
 * 伪代码：遍历每个字符 -> 按 switch 转义 -> 拼接 "input text " + shellQuote(escaped)
 */
std::string inputTextDirect(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case ' ': escaped += "%s"; break;
            case '%': escaped += "%25"; break;
            default: escaped += c;
        }
    }
    return "input text " + util::shellQuote(escaped);
}

/**
 * @brief 生成启动应用命令（双保险策略）
 * @param packageName 应用包名
 * @return 复合 shell 命令串
 * 实现思路：
 *   1. 先 monkey 冷启动（兼容老设备），grep -q . 吞掉输出；
 *   2. 再用 cmd package resolve-activity 解析主 Activity 精确 am start；
 *   3. 解析失败（如老系统无 cmd 命令）回退 monkey。
 */
std::string launchApp(const std::string& packageName) {
    return "monkey -p " + util::shellQuote(packageName) +
           " -c android.intent.category.LAUNCHER 1 2>&1 | grep -q . ; "
           "am start -n \"$(cmd package resolve-activity --brief " +
           util::shellQuote(packageName) +
           " | tail -n 1)\" 2>/dev/null || monkey -p " + util::shellQuote(packageName) +
           " -c android.intent.category.LAUNCHER 1";
}

/**
 * @brief 生成强制停止应用命令
 * @param packageName 应用包名
 * @return "am force-stop <package>"（包名已 shellQuote）
 */
std::string closeApp(const std::string& packageName) {
    return "am force-stop " + util::shellQuote(packageName);
}

/**
 * @brief 生成列出应用包名命令
 * @param thirdPartyOnly true 仅列第三方应用（-3），false 列全部
 * @return "pm list packages [-3]"
 */
std::string listPackages(bool thirdPartyOnly) {
    return thirdPartyOnly ? "pm list packages -3" : "pm list packages";
}

/**
 * @brief 生成"下载 + 安装 + 清理"复合命令
 * @param downloadUrl APK 下载地址 @param localPath 设备侧临时存放路径
 * @return 复合 shell 命令串
 * 实现思路：
 *   1. 优先 curl -fSL 下载，不存在则回退 wget -O；
 *   2. 下载失败（rc != 0）删除残留文件并以原错误码退出；
 *   3. pm install -r 覆盖安装，记录其退出码，无论如何删除临时 apk，
 *      最终以 pm install 的退出码作为整条命令的退出码。
 * 伪代码：download || fallback -> 失败清理退出 -> install -> 清理 -> exit rc
 */
std::string installApk(const std::string& downloadUrl, const std::string& localPath) {
    std::string path = util::shellQuote(localPath);
    std::string url = util::shellQuote(downloadUrl);
    return "(command -v curl >/dev/null 2>&1 && curl -fSL -o " + path + " " + url + ") || "
           "(command -v wget >/dev/null 2>&1 && wget -O " + path + " " + url + "); "
           "rc=$?; if [ $rc -ne 0 ]; then rm -f " + path + "; echo 'download failed'; exit $rc; fi; "
           "pm install -r " + path + "; rc=$?; rm -f " + path + "; exit $rc";
}

/**
 * @brief 判断文本是否全部为 ASCII 可见字符（0x20~0x7E）
 * @param text 待检测文本
 * @return true 表示可走 `input text` 直输路径；false 需走广播注入
 * 伪代码：逐字节检查，任一字符越界即返回 false
 */
bool isAsciiPrintable(const std::string& text) {
    for (unsigned char c : text) {
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

}  // namespace cmd
}  // namespace service
