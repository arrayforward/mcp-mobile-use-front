/**
 * @file executor.hpp
 * @brief 命令执行器抽象层——本地 shell 与云手机 HTTP 后端的统一接口
 *
 * 功能：
 *   定义执行结果结构体 ExecResult（退出码/超时标志/stdout/stderr/错误信息），
 *   以及抽象基类 Executor（runShell 执行 shell 命令 + name 后端名称），
 *   并声明两个工厂函数用于创建本地（adb/本机 shell）与云端执行器实例。
 *
 * 开发思路：
 *   1. 用纯虚接口屏蔽"本地 fork+exec"与"云端 HTTPS API"两种实现差异，
 *      Provider 层只面向 Executor 编程，按 backend 参数动态路由。
 *   2. ExecResult 将"传输错误"（error 字段）与"命令失败"（exitCode != 0）分开，
 *      便于上层区分网络故障与设备侧业务失败。
 *   3. 本地执行器以函数内静态单例返回（无状态），云端执行器按配置 new 实例
 *      （持有 CloudConfig），生命周期由 Provider 的 unique_ptr 管理。
 *
 * @author hubin
 * @date 2026-08-05
 */
#pragma once

#include <string>

namespace service {

/**
 * @struct ExecResult
 * @brief 一次 shell 命令执行的完整结果
 *
 * 开发思路：
 *   error 表示"框架级失败"（管道创建失败、网络错误、被信号杀死等），
 *   timedOut 单独标记超时场景，exitCode 仅在进程正常回收后才有意义；
 *   ok() 将三者合一作为"命令成功"的判定标准。
 */
struct ExecResult {
    int exitCode = -1;      // 进程退出码，-1 表示未取得（错误/超时/被信号杀死）
    bool timedOut = false;  // 是否因超时被 SIGKILL 强杀
    std::string out;        // 子进程 stdout 全部输出
    std::string err;        // 子进程 stderr 全部输出
    std::string error;      // 框架级错误描述（非空即失败）

    /** @brief 判定命令是否成功：无框架错误、未超时、退出码为 0 */
    bool ok() const { return error.empty() && !timedOut && exitCode == 0; }
    /** @brief 生成人类可读的失败描述（成功时返回空串），见 local_executor.cpp */
    std::string describe() const;
};

/**
 * @class Executor
 * @brief 命令执行器抽象基类
 *
 * 开发思路：
 *   只暴露 runShell 一个动作接口，本地实现走 fork+execvp+pipe+poll，
 *   云端实现走 POST sync-commands REST API，二者对调用方完全等价。
 */
class Executor {
public:
    virtual ~Executor() = default;
    /**
     * @brief 执行一条 shell 命令
     * @param shellCmd 完整 shell 命令串（由 CommandBuilder 生成）
     * @param timeoutMs 超时毫秒数，超时后强杀并置 timedOut
     * @return ExecResult 执行结果
     */
    virtual ExecResult runShell(const std::string& shellCmd, int timeoutMs) = 0;
    /** @brief 后端名称（"adb"/"cloud"），用于日志与错误提示 */
    virtual const char* name() const = 0;
};

struct CloudConfig;

/** @brief 创建本地执行器（函数内静态单例，无需释放） */
Executor* createLocalExecutor();
/** @brief 创建云端执行器（new 实例，调用方负责 delete） */
Executor* createCloudExecutor(const CloudConfig& config);

}  // namespace service
