#pragma once

#include <string>

namespace service {

struct ExecResult {
    int exitCode = -1;
    bool timedOut = false;
    std::string out;
    std::string err;
    std::string error;

    bool ok() const { return error.empty() && !timedOut && exitCode == 0; }
    std::string describe() const;
};

class Executor {
public:
    virtual ~Executor() = default;
    virtual ExecResult runShell(const std::string& shellCmd, int timeoutMs) = 0;
    virtual const char* name() const = 0;
};

struct CloudConfig;

Executor* createLocalExecutor();
Executor* createCloudExecutor(const CloudConfig& config);

}  // namespace service
