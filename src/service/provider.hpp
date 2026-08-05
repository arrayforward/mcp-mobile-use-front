/**
 * @file provider.hpp
 * @brief MCP 手机操控服务门面——按 backend 路由到本地/云端执行器
 *
 * 功能：
 *   定义 Backend 枚举（Adb/Cloud）与 MobileUseProvider 类，对上（MCP 工具
 *   注册层）提供截图、点击、滑动、文本输入、按键、应用管理等高级操作，
 *   对下通过 CommandBuilder 生成 shell 命令，再按 backend 参数路由到
 *   LocalExecutor 或 CloudExecutor 执行；runShell 为透传任意命令的通用入口。
 *
 * 开发思路：
 *   1. 统一双后端抽象：每个工具方法第一个参数都是 Backend，
 *      由 executor(b) 取得对应执行器，业务逻辑两后端共用一份代码；
 *   2. 本地执行器为全局静态单例（m_local 裸指针不持有所有权），
 *      云端执行器按需懒创建并由 unique_ptr 管理生命周期；
 *   3. 每个方法返回 bool + out 参数 err，把 ExecResult 细节收敛成
 *      "成功/失败 + 错误描述"，简化 MCP 工具层的响应组装。
 *
 * @author hubin
 * @date 2026-08-05
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../json/json.hpp"
#include "cloud_config.hpp"
#include "executor.hpp"

namespace service {

/** @brief 后端类型：Adb=本机 shell（经 adb 通道），Cloud=云手机 REST API */
enum class Backend { Adb, Cloud };

/** @brief 字符串转 Backend（"cloud"/"adb"/"local"，无法识别返回 def） */
Backend backendFromString(const std::string& s, Backend def);
/** @brief Backend 转字符串（"cloud"/"adb"） */
std::string backendToString(Backend b);

/**
 * @class MobileUseProvider
 * @brief 手机操控服务门面：12 个 MCP 工具的业务实现入口
 *
 * 开发思路：
 *   持有一个默认后端 + 两个执行器指针；工具方法全部走
 *   "CommandBuilder 拼命令 -> executor(b)->runShell -> 解析输出"三段式，
 *   只有截图等少数方法因通道差异（stdout 二进制 vs 设备落盘）做分支处理。
 */
class MobileUseProvider {
public:
    /** @brief 构造并指定默认后端（m_local 立即就绪，m_cloud 懒创建） */
    explicit MobileUseProvider(Backend defaultBackend);
    ~MobileUseProvider();

    /** @brief 注入云端配置；配置有效时立即创建云端执行器 */
    void setCloudConfig(const CloudConfig& config);

    /** @brief 当前默认后端 */
    Backend defaultBackend() const { return m_default; }
    /**
     * @brief 从 MCP 工具入参解析目标后端
     * @param args 工具参数 JSON（含可选 "backend" 字段）
     * @return 参数指定则用之，否则返回默认后端
     */
    Backend resolveBackend(const mj::Value& args) const;

    /** @brief 截图结果：base64 PNG（本地）或设备文件路径（云端）+ 尺寸 */
    struct Screenshot {
        std::string base64Png;
        std::string devicePath;
        int width = 0;
        int height = 0;
    };

    /** @brief 应用条目（目前仅包名） */
    struct AppItem {
        std::string packageName;
    };

    /** @brief 截屏：本地走 stdout 回读 base64，云端走设备落盘 @return 成功 true */
    bool screenshot(Backend b, Screenshot& shot, std::string& err);
    /** @brief 点击坐标 (x,y) */
    bool tap(Backend b, int x, int y, std::string& err);
    /** @brief 从 (x1,y1) 滑动到 (x2,y2)，耗时 durationMs 毫秒 */
    bool swipe(Backend b, int x1, int y1, int x2, int y2, int durationMs, std::string& err);
    /** @brief 输入文本：ASCII 走 input text，非 ASCII 走广播注入（失败回退直输） */
    bool inputText(Backend b, const std::string& text, std::string& err);
    /** @brief 发送按键事件（Android keyCode） */
    bool keyEvent(Backend b, int keyCode, std::string& err);
    /** @brief 按包名启动应用 */
    bool launchApp(Backend b, const std::string& packageName, std::string& err);
    /** @brief 按包名强制停止应用 */
    bool closeApp(Backend b, const std::string& packageName, std::string& err);
    /** @brief 列出应用包名（thirdPartyOnly 仅第三方） */
    bool listApps(Backend b, bool thirdPartyOnly, std::vector<AppItem>& apps, std::string& err);
    /** @brief 从 URL 下载并安装 APK（超时放宽到 300s） */
    bool installApp(Backend b, const std::string& downloadUrl, std::string& err);

    // 标准 adb shell 接口：执行任意 shell 命令（对齐 adb shell <cmd>）
    /**
     * @brief 通用入口：透传执行任意 shell 命令
     * @param command 命令串 @param timeoutMs 超时（<=0 用默认 30s）
     * @param out 完整执行结果 @param err 失败描述
     * @return 无框架级错误返回 true（命令退出码非 0 仍算 true，见 out.exitCode）
     */
    bool runShell(Backend b, const std::string& command, int timeoutMs, ExecResult& out,
                  std::string& err);

private:
    /** @brief 按后端取执行器（云端未建则懒创建） */
    Executor* executor(Backend b);
    /** @brief 执行 wm size 并解析出宽高 */
    bool readScreenSize(Backend b, int& width, int& height);

    Backend m_default;                  // 默认后端
    Executor* m_local;                  // 本地执行器（全局单例，不持有所有权）
    std::unique_ptr<Executor> m_cloud;  // 云端执行器（持有所有权，懒创建）
    CloudConfig m_cloudConfig;          // 云端配置副本
    int m_defaultTimeoutMs = 30000;     // 默认命令超时 30 秒
};

}  // namespace service
