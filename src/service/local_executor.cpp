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

void setNonBlock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

}  // namespace

class LocalExecutor : public Executor {
public:
    const char* name() const override { return "adb"; }

    ExecResult runShell(const std::string& shellCmd, int timeoutMs) override {
        ExecResult result;

        int outPipe[2] = {-1, -1};
        int errPipe[2] = {-1, -1};
        if (pipe(outPipe) != 0 || pipe(errPipe) != 0) {
            result.error = std::string("pipe failed: ") + strerror(errno);
            return result;
        }

        pid_t pid = fork();
        if (pid < 0) {
            result.error = std::string("fork failed: ") + strerror(errno);
            close(outPipe[0]); close(outPipe[1]);
            close(errPipe[0]); close(errPipe[1]);
            return result;
        }

        if (pid == 0) {
            dup2(outPipe[1], STDOUT_FILENO);
            dup2(errPipe[1], STDERR_FILENO);
            close(outPipe[0]); close(outPipe[1]);
            close(errPipe[0]); close(errPipe[1]);
            execl("/system/bin/sh", "sh", "-c", shellCmd.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }

        close(outPipe[1]);
        close(errPipe[1]);
        setNonBlock(outPipe[0]);
        setNonBlock(errPipe[0]);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        bool outOpen = true, errOpen = true;
        bool childDone = false;
        int status = 0;

        while (outOpen || errOpen) {
            if (!childDone) {
                pid_t w = waitpid(pid, &status, WNOHANG);
                if (w == pid) childDone = true;
            }
            if (!childDone && std::chrono::steady_clock::now() > deadline) {
                result.timedOut = true;
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                childDone = true;
            }

            struct pollfd fds[2];
            nfds_t n = 0;
            if (outOpen) { fds[n].fd = outPipe[0]; fds[n].events = POLLIN; fds[n].revents = 0; n++; }
            if (errOpen) { fds[n].fd = errPipe[0]; fds[n].events = POLLIN; fds[n].revents = 0; n++; }
            int timeout = childDone ? 100 : 50;
            int rv = poll(fds, n, timeout);
            if (rv <= 0) {
                if (childDone) {
                    if (outOpen) { drainFd(outPipe[0], result.out); outOpen = false; close(outPipe[0]); }
                    if (errOpen) { drainFd(errPipe[0], result.err); errOpen = false; close(errPipe[0]); }
                }
                continue;
            }
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

        if (!result.timedOut) {
            if (WIFEXITED(status))
                result.exitCode = WEXITSTATUS(status);
            else
                result.exitCode = -1;
        }
        return result;
    }

private:
    static bool drainFd(int fd, std::string& buf) {
        char tmp[8192];
        bool any = false;
        while (true) {
            ssize_t r = read(fd, tmp, sizeof(tmp));
            if (r > 0) {
                buf.append(tmp, static_cast<size_t>(r));
                any = true;
            } else if (r == 0) {
                return false;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
                return false;
            }
        }
    }
};

Executor* createLocalExecutor() {
    static LocalExecutor instance;
    return &instance;
}

}  // namespace service
