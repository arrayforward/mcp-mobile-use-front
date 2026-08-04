#pragma once

#include <memory>
#include <string>
#include <vector>

#include "../json/json.hpp"
#include "cloud_config.hpp"
#include "executor.hpp"

namespace service {

enum class Backend { Adb, Cloud };

Backend backendFromString(const std::string& s, Backend def);
std::string backendToString(Backend b);

class MobileUseProvider {
public:
    explicit MobileUseProvider(Backend defaultBackend);
    ~MobileUseProvider();

    void setCloudConfig(const CloudConfig& config);

    Backend defaultBackend() const { return default_; }
    Backend resolveBackend(const mj::Value& args) const;

    struct Screenshot {
        std::string base64Png;
        std::string devicePath;
        int width = 0;
        int height = 0;
    };

    struct AppItem {
        std::string packageName;
    };

    bool screenshot(Backend b, Screenshot& shot, std::string& err);
    bool tap(Backend b, int x, int y, std::string& err);
    bool swipe(Backend b, int x1, int y1, int x2, int y2, int durationMs, std::string& err);
    bool inputText(Backend b, const std::string& text, std::string& err);
    bool keyEvent(Backend b, int keyCode, std::string& err);
    bool launchApp(Backend b, const std::string& packageName, std::string& err);
    bool closeApp(Backend b, const std::string& packageName, std::string& err);
    bool listApps(Backend b, bool thirdPartyOnly, std::vector<AppItem>& apps, std::string& err);
    bool installApp(Backend b, const std::string& downloadUrl, std::string& err);

private:
    Executor* executor(Backend b);
    bool readScreenSize(Backend b, int& width, int& height);

    Backend default_;
    Executor* local_;
    std::unique_ptr<Executor> cloud_;
    CloudConfig cloudConfig_;
    int defaultTimeoutMs_ = 30000;
};

}  // namespace service
