#include "cloud_config.hpp"

#include <cstdlib>

namespace service {

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

}  // namespace

std::string CloudConfig::missingHint() const {
    return "cloud backend not configured; set env CPH_ENDPOINT, CPH_PROJECT_ID, "
           "CPH_PHONE_ID and either CPH_TOKEN or CPH_AK+CPH_SK";
}

CloudConfig CloudConfig::fromEnv() {
    CloudConfig cfg;
    cfg.endpoint = env("CPH_ENDPOINT");
    cfg.projectId = env("CPH_PROJECT_ID");
    cfg.phoneId = env("CPH_PHONE_ID");
    cfg.token = env("CPH_TOKEN");
    cfg.ak = env("CPH_AK");
    cfg.sk = env("CPH_SK");
    return cfg;
}

}  // namespace service
