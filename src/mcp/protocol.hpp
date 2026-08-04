#pragma once

#include <string>

#include "../json/json.hpp"

namespace mcp {

class Protocol {
public:
    Protocol();

    bool handleMessage(const std::string& requestJson, std::string& responseJson);

    static const char* protocolVersion();
    static const char* serverName();
    static const char* serverVersion();

private:
    mj::Value dispatch(const mj::Value& id, const std::string& method,
                       const mj::Value& params);
    mj::Value onInitialize();
    mj::Value onToolsList();
    mj::Value onToolsCall(const mj::Value& params);

    mj::Value makeResult(const mj::Value& id, const mj::Value& result);
    mj::Value makeError(const mj::Value& id, int code, const std::string& message);
};

}  // namespace mcp
