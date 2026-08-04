#pragma once

#include <string>

namespace service {

struct CloudConfig {
    std::string endpoint;
    std::string projectId;
    std::string phoneId;
    std::string token;
    std::string ak;
    std::string sk;

    bool valid() const {
        return !endpoint.empty() && !projectId.empty() && !phoneId.empty() &&
               (!token.empty() || (!ak.empty() && !sk.empty()));
    }

    std::string missingHint() const;

    static CloudConfig fromEnv();
};

}  // namespace service
