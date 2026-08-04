#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../json/json.hpp"
#include "../service/provider.hpp"

namespace tool {

void initProvider(service::Backend defaultBackend, const service::CloudConfig& cloudConfig);
service::MobileUseProvider& provider();

struct ToolDef {
    std::string name;
    std::string description;
    mj::Value inputSchema;
    std::function<mj::Value(const mj::Value&)> handler;
};

std::vector<ToolDef> allTools();

mj::Value textResult(const std::string& text);
mj::Value errorResult(const std::string& msg);
mj::Value imageResult(const std::string& base64Png);

mj::Value makeSchema(const mj::Value& properties, const std::vector<std::string>& required);
mj::Value propNumber(const std::string& desc);
mj::Value propString(const std::string& desc);
mj::Value propBool(const std::string& desc);

service::Backend pickBackend(const mj::Value& args);
bool getInt(const mj::Value& args, const char* key, int& out, std::string& err);
bool getRequiredString(const mj::Value& args, const char* key, std::string& out,
                       std::string& err);

}  // namespace tool
