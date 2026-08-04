#pragma once

#include <map>
#include <string>

namespace service {

std::string hwSignAuthorization(const std::string& ak, const std::string& sk,
                                const std::string& method, const std::string& host,
                                const std::string& uri, const std::string& body,
                                const std::string& sdkDate);

std::string hwSdkDateNow();

bool hwSignerAvailable();

}  // namespace service
