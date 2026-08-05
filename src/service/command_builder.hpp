/**
 * @file command_builder.hpp
 * @brief 设备 shell 命令模板库——12 个 MCP 工具的 Android shell 命令生成器
 *
 * 功能：
 *   为每个 MCP 工具（点击/滑动/按键/截图/屏幕尺寸/文本输入/启动关闭应用/
 *   列出应用/安装 APK 等）生成对应的 Android shell 命令字符串，
 *   命令最终由 Executor（本地 adb shell 或云手机 sync-commands）执行。
 *
 * 开发思路：
 *   1. 命令拼装与执行解耦：本模块只负责"生成字符串"，不关心传输通道，
 *      便于单元测试与命令审计。
 *   2. 所有外部输入（路径/包名/文本/URL）一律经 util::shellQuote 转义，
 *      防止 shell 注入。
 *   3. 文本输入提供两条路径：ASCII 可见字符走 `input text`（快），
 *      非 ASCII（如中文）走 IME 切换 + 广播注入（兼容性好）。
 *
 * @author hubin
 * @date 2026-08-05
 */
#pragma once

#include <string>

namespace service {
namespace cmd {

/** @brief 生成点击命令：input tap x y */
std::string tap(int x, int y);
/** @brief 生成滑动命令：input swipe x1 y1 x2 y2 durationMs */
std::string swipe(int x1, int y1, int x2, int y2, int durationMs);
/** @brief 生成按键命令：input keyevent keyCode */
std::string keyEvent(int keyCode);
/** @brief 生成截图命令（PNG 输出到 stdout）：screencap -p */
std::string screenshotStdout();
/** @brief 生成截图到设备文件的命令：screencap -p <path>（云端无 stdout 二进制通道时用） */
std::string screenshotToFile(const std::string& path);
/** @brief 生成删除设备文件命令：rm -f <path> */
std::string removeFile(const std::string& path);
/** @brief 生成查询屏幕尺寸命令：wm size */
std::string screenSize();
/** @brief 生成切换默认输入法命令（为广播注入文本做准备） */
std::string textInputSelectIme();
/** @brief 生成清空输入框广播命令 */
std::string textInputClear();
/** @brief 生成广播注入文本命令（支持中文等非 ASCII 文本） */
std::string textInputBroadcast(const std::string& text);
/** @brief 生成 input text 直输命令（空格转 %s、% 转 %25，仅适用 ASCII） */
std::string inputTextDirect(const std::string& text);
/** @brief 生成启动应用命令（monkey 冷启动 + am start 精确启动双保险） */
std::string launchApp(const std::string& packageName);
/** @brief 生成强制停止应用命令：am force-stop <package> */
std::string closeApp(const std::string& packageName);
/** @brief 生成列出应用包名命令：pm list packages [-3] */
std::string listPackages(bool thirdPartyOnly);
/** @brief 生成下载并安装 APK 的复合命令（curl/wget 下载 + pm install + 清理） */
std::string installApk(const std::string& downloadUrl, const std::string& localPath);

/** @brief 判断文本是否全部为 ASCII 可见字符（决定走 input text 还是广播注入） */
bool isAsciiPrintable(const std::string& text);

}  // namespace cmd
}  // namespace service
