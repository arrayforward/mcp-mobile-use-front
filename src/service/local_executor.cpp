/**
 * @file local_executor.cpp
 * @brief 本地命令执行器——fork + execl("sh","-c") + pipe 收集输出 + poll 超时控制
 *
 * 功能：
 *   实现 LocalExecutor：在本机（或已 adb 转发的设备 shell 环境）通过
 *   fork 子进程执行 `/system/bin/sh -c <cmd>`，用两根管道分别回收
 *   stdout/stderr，poll 非阻塞读取并支持毫秒级超时强杀；
 *   同时实现 ExecResult::describe() 失败描述生成。
 *
 * 开发思路：
 *   1. 管道读端设 O_NONBLOCK + poll 事件驱动，避免子进程输出量大于
 *      管道缓冲（64K）时父进程读阻塞、子进程写阻塞的经典死锁。
 *   2. 主循环内用 waitpid(WNOHANG) 探测子进程存活，超过 deadline 即
 *      SIGKILL 并回收，置 timedOut。
 *   3. 关键 bug 修复（务必保留）：当两根管道都读到 EOF 退出主循环后，
 *      必须再阻塞 waitpid(pid, &status, 0) 等待子进程真正退出，
 *      否则 status 未被填充，WEXITSTATUS 读到的是初始值 0，
 *      导致失败命令的退出码被误报为 0。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "executor.hpp"

#include "../util/util.hpp"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>

namespace service {

/**
 * @brief 生成人类可读的执行失败描述
 * @return 失败原因串；成功时返回空串
 * 伪代码：优先 error -> 其次 timedOut -> 其次非零退出码（附 stderr/stdout 摘要）
 */
std::string ExecResult::describe() const {
    if (!error.empty()) return error;
    if (timedOut) return "command timed out";
    if (exitCode != 0) {
        std::string msg = "command exited with code " + std::to_string(exitCode);
        std::string e = util::trim(err);
        std::string o = util::trim(out);
        if (!e.empty()) msg += ": " + e;
        else if (!o.empty()) msg += ": " + o;
        return msg;
    }
    return "";
}

namespace {

/**
 * @brief 将文件描述符设为非阻塞模式
 * @param fd 目标 fd（管道读端）
 * 伪代码：F_GETFL 取原标志 -> 追加 O_NONBLOCK -> F_SETFL 写回
 */
void setNonBlock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

}  // namespace

/**
 * @class LocalExecutor
 * @brief 本地 shell 执行器（adb 后端）
 *
 * 开发思路：
 *   fork 子进程 execl("/system/bin/sh","sh","-c",cmd)，父进程通过
 *   pipe + 非阻塞 poll 收集 stdout/stderr 并实施超时强杀；
 *   以函数内静态单例对外提供（见 createLocalExecutor），无状态可全局共享。
 */
class LocalExecutor : public Executor {
public:
    /** @brief 后端名称："adb" */
    const char* name() const override { return "adb"; }

    /**
     * @brief 本地执行一条 shell 命令
     * @param shellCmd 完整 shell 命令串
     * @param timeoutMs 超时毫秒数，超时 SIGKILL 强杀并置 timedOut
     * @return ExecResult（exitCode/stdout/stderr/timedOut/error）
     *
     * 实现思路：
     *   1. 建两根管道（stdout/stderr 各一），失败直接报错返回；
     *   2. fork：子进程 dup2 重定向后 execl sh -c，exec 失败 _exit(127)；
     *   3. 父进程关闭写端、读端设非阻塞，进入 poll 主循环：
     *      - WNOHANG 探测子进程是否退出；超 deadline 则 SIGKILL 回收；
     *      - poll 等待两管道可读/挂断，drainFd 把数据追加进结果；
     *      - 子进程已退出但管道还有残留数据时继续 drain 直到 EOF；
     *   4. 循环结束后若未超时，必须阻塞 waitpid 取回真实退出状态
     *      （修复过的 bug：不阻塞等待则 status 未填充，退出码误报为 0）；
     *   5. WIFEXITED 取退出码，WIFSIGNALED 记录信号错误。
     *
     * 伪代码：
     *   pipe x2 -> fork -> 子: dup2/exec -> 父: poll 循环读管道+查超时
     *   -> 管道 EOF 后阻塞 waitpid -> 解析 status 填 result
     */
    ExecResult runShell(const std::string& shellCmd, int timeoutMs) override {
        ExecResult result;

        // 第一步：创建 stdout/stderr 两根管道
        int outPipe[2] = {-1, -1};
        int errPipe[2] = {-1, -1};
        if (pipe(outPipe) != 0 || pipe(errPipe) != 0) {
            result.error = std::string("pipe failed: ") + strerror(errno);
            return result;
        }

        // 第二步：fork 子进程
        pid_t pid = fork();
        if (pid < 0) {
            result.error = std::string("fork failed: ") + strerror(errno);
            close(outPipe[0]); close(outPipe[1]);
            close(errPipe[0]); close(errPipe[1]);
            return result;
        }

        if (pid == 0) {
            // 子进程：把管道写端接到 stdout/stderr，再 exec sh -c
            dup2(outPipe[1], STDOUT_FILENO);
            dup2(errPipe[1], STDERR_FILENO);
            close(outPipe[0]); close(outPipe[1]);
            close(errPipe[0]); close(errPipe[1]);
            execl("/system/bin/sh", "sh", "-c", shellCmd.c_str(), static_cast<char*>(nullptr));
            _exit(127);  // exec 失败：127 与 shell 的 "command not found" 语义一致
        }

        // 父进程：关闭写端（子进程退出后读端才能收到 EOF），读端设非阻塞
        close(outPipe[1]);
        close(errPipe[1]);
        setNonBlock(outPipe[0]);
        setNonBlock(errPipe[0]);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        bool outOpen = true, errOpen = true;
        bool childDone = false;
        int status = 0;

        // 第三步：poll 主循环——直到两根管道都读到 EOF（子进程关闭写端）
        while (outOpen || errOpen) {
            // 非阻塞探测子进程是否已退出
            if (!childDone) {
                pid_t w = waitpid(pid, &status, WNOHANG);
                if (w == pid) childDone = true;
            }
            // 超时判定：强杀并同步回收，避免留下僵尸进程
            if (!childDone && std::chrono::steady_clock::now() > deadline) {
                result.timedOut = true;
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                childDone = true;
            }

            // 组装 pollfd 数组（只监视仍打开的管道）
            struct pollfd fds[2];
            nfds_t n = 0;
            if (outOpen) { fds[n].fd = outPipe[0]; fds[n].events = POLLIN; fds[n].revents = 0; n++; }
            if (errOpen) { fds[n].fd = errPipe[0]; fds[n].events = POLLIN; fds[n].revents = 0; n++; }
            int timeout = childDone ? 100 : 50;  // 子进程未走时用较短间隔以便及时检查超时
            int rv = poll(fds, n, timeout);
            if (rv <= 0) {
                // 超时/出错：子进程已结束时说明管道不会再有数据，直接排空并关闭
                if (childDone) {
                    if (outOpen) { drainFd(outPipe[0], result.out); outOpen = false; close(outPipe[0]); }
                    if (errOpen) { drainFd(errPipe[0], result.err); errOpen = false; close(errPipe[0]); }
                }
                continue;
            }
            // 有事件：可读或挂断（POLLHUP）都尝试 drain，EOF 后关闭对应管道
            nfds_t idx = 0;
            if (outOpen) {
                if (fds[idx].revents & (POLLIN | POLLHUP | POLLERR)) {
                    if (!drainFd(outPipe[0], result.out)) { outOpen = false; close(outPipe[0]); }
                }
                idx++;
            }
            if (errOpen) {
                if (fds[idx].revents & (POLLIN | POLLHUP | POLLERR)) {
                    if (!drainFd(errPipe[0], result.err)) { errOpen = false; close(errPipe[0]); }
                }
            }
        }

        // 第四步：管道已全部 EOF，但子进程可能尚未真正退出。
        // 【关键 bug 修复】此处必须阻塞 waitpid 等子进程退出：
        // 否则 status 保持初始值 0，WEXITSTATUS(0)=0，
        // 会把失败命令的退出码误报为 0。
        if (!result.timedOut) {
            if (!childDone) {
                waitpid(pid, &status, 0);
                childDone = true;
            }
            // 第五步：解析退出状态
            if (WIFEXITED(status)) {
                result.exitCode = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                result.exitCode = -1;
                result.error = std::string("command killed by signal ") +
                               std::to_string(WTERMSIG(status));
            } else {
                result.exitCode = -1;
            }
        }
        return result;
    }

private:
    /**
     * @brief 把 fd 上当前可读的数据全部读入 buf（非阻塞）
     * @param fd 非阻塞管道读端 @param buf 输出累积缓冲
     * @return true 表示管道仍打开（读到数据或暂无数据 EAGAIN）；
     *         false 表示已到 EOF 或出错，调用方应关闭该 fd
     * 伪代码：循环 read -> >0 追加 -> ==0 返回 false(EOF) -> EAGAIN 返回 true
     */
    static bool drainFd(int fd, std::string& buf) {
        char tmp[8192];
        bool any = false;
        while (true) {
            ssize_t r = read(fd, tmp, sizeof(tmp));
            if (r > 0) {
                buf.append(tmp, static_cast<size_t>(r));
                any = true;
            } else if (r == 0) {
                return false;  // EOF：对端写端已全部关闭
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return true;  // 暂无数据
                return false;  // 真正读错误，视为关闭
            }
        }
    }
};

/**
 * @brief 创建本地执行器（Meyers 单例，线程安全且无需释放）
 * @return 全局唯一的 LocalExecutor 指针
 */
Executor* createLocalExecutor() {
    static LocalExecutor instance;
    return &instance;
}

}  // namespace service
