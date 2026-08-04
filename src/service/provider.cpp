#include "provider.hpp"

#include "../util/util.hpp"
#include "command_builder.hpp"

#include <cstdlib>

namespace service {

Backend backendFromString(const std::string& s, Backend def) {
    if (s == "cloud") return Backend::Cloud;
    if (s == "adb" || s == "local") return Backend::Adb;
    return def;
}

std::string backendToString(Backend b) {
    return b == Backend::Cloud ? "cloud" : "adb";
}

MobileUseProvider::MobileUseProvider(Backend defaultBackend)
    : default_(defaultBackend), local_(createLocalExecutor()) {}

MobileUseProvider::~MobileUseProvider() = default;

void MobileUseProvider::setCloudConfig(const CloudConfig& config) {
    cloudConfig_ = config;
    if (config.valid()) cloud_ = std::unique_ptr<Executor>(createCloudExecutor(config));
}

Backend MobileUseProvider::resolveBackend(const mj::Value& args) const {
    if (args.isObject() && args.has("backend"))
        return backendFromString(args["backend"].asString(), default_);
    return default_;
}

Executor* MobileUseProvider::executor(Backend b) {
    if (b == Backend::Cloud) {
        if (cloud_) return cloud_.get();
        cloud_ = std::unique_ptr<Executor>(createCloudExecutor(cloudConfig_));
        return cloud_.get();
    }
    return local_;
}

bool MobileUseProvider::readScreenSize(Backend b, int& width, int& height) {
    ExecResult r = executor(b)->runShell(cmd::screenSize(), defaultTimeoutMs_);
    if (!r.ok()) return false;
    size_t pos = r.out.find("Physical size:");
    if (pos == std::string::npos) {
        pos = r.out.find("Override size:");
        if (pos == std::string::npos) return false;
        pos += 14;
    } else {
        pos += 14;
    }
    if (sscanf(r.out.c_str() + pos, "%dx%d", &width, &height) != 2) return false;
    return width > 0 && height > 0;
}

bool MobileUseProvider::screenshot(Backend b, Screenshot& shot, std::string& err) {
    if (b == Backend::Cloud) {
        const std::string path = "/sdcard/.mcp_mobile_use_shot.png";
        ExecResult r = executor(b)->runShell(
            cmd::screenshotToFile(path) + " && " + cmd::screenSize() + " && " +
                cmd::removeFile(path),
            defaultTimeoutMs_);
        if (!r.ok()) {
            err = "cloud screenshot failed: " + r.describe();
            return false;
        }
        shot.devicePath = path;
        int w = 0, h = 0;
        if (sscanf(strstr(r.out.c_str(), "Physical size:") ?
                           strstr(r.out.c_str(), "Physical size:") + 14 : "",
                       "%dx%d", &w, &h) == 2) {
            shot.width = w;
            shot.height = h;
        }
        return true;
    }

    ExecResult r = executor(b)->runShell(cmd::screenshotStdout(), defaultTimeoutMs_);
    if (!r.ok()) {
        err = "screenshot failed: " + r.describe();
        return false;
    }
    const std::string kPngMagic = "\x89PNG";
    if (r.out.size() < 8 || r.out.compare(0, 4, kPngMagic) != 0) {
        err = "screenshot output is not png: " + r.out.substr(0, 128);
        return false;
    }
    shot.base64Png = util::base64Encode(r.out);
    readScreenSize(b, shot.width, shot.height);
    return true;
}

bool MobileUseProvider::tap(Backend b, int x, int y, std::string& err) {
    ExecResult r = executor(b)->runShell(cmd::tap(x, y), defaultTimeoutMs_);
    if (!r.ok()) {
        err = "tap failed: " + r.describe();
        return false;
    }
    return true;
}

bool MobileUseProvider::swipe(Backend b, int x1, int y1, int x2, int y2, int durationMs,
                              std::string& err) {
    ExecResult r = executor(b)->runShell(cmd::swipe(x1, y1, x2, y2, durationMs),
                                         defaultTimeoutMs_ + durationMs);
    if (!r.ok()) {
        err = "swipe failed: " + r.describe();
        return false;
    }
    return true;
}

bool MobileUseProvider::inputText(Backend b, const std::string& text, std::string& err) {
    if (cmd::isAsciiPrintable(text)) {
        ExecResult r = executor(b)->runShell(cmd::inputTextDirect(text), defaultTimeoutMs_);
        if (r.ok()) return true;
        err = "input text failed: " + r.describe();
        return false;
    }
    ExecResult ime = executor(b)->runShell(cmd::textInputSelectIme(), defaultTimeoutMs_);
    (void)ime;
    ExecResult clear = executor(b)->runShell(cmd::textInputClear(), defaultTimeoutMs_);
    (void)clear;
    ExecResult r = executor(b)->runShell(cmd::textInputBroadcast(text), defaultTimeoutMs_);
    if (r.ok()) return true;
    ExecResult r2 = executor(b)->runShell(cmd::inputTextDirect(text), defaultTimeoutMs_);
    if (r2.ok()) return true;
    err = "input text failed: broadcast: " + r.describe() + "; fallback: " + r2.describe();
    return false;
}

bool MobileUseProvider::keyEvent(Backend b, int keyCode, std::string& err) {
    ExecResult r = executor(b)->runShell(cmd::keyEvent(keyCode), defaultTimeoutMs_);
    if (!r.ok()) {
        err = "key event failed: " + r.describe();
        return false;
    }
    return true;
}

bool MobileUseProvider::launchApp(Backend b, const std::string& packageName, std::string& err) {
    ExecResult r = executor(b)->runShell(cmd::launchApp(packageName), defaultTimeoutMs_);
    if (!r.ok()) {
        err = "launch app failed: " + r.describe();
        return false;
    }
    return true;
}

bool MobileUseProvider::closeApp(Backend b, const std::string& packageName, std::string& err) {
    ExecResult r = executor(b)->runShell(cmd::closeApp(packageName), defaultTimeoutMs_);
    if (!r.ok()) {
        err = "close app failed: " + r.describe();
        return false;
    }
    return true;
}

bool MobileUseProvider::listApps(Backend b, bool thirdPartyOnly, std::vector<AppItem>& apps,
                                 std::string& err) {
    ExecResult r = executor(b)->runShell(cmd::listPackages(thirdPartyOnly), defaultTimeoutMs_);
    if (!r.ok()) {
        err = "list apps failed: " + r.describe();
        return false;
    }
    size_t pos = 0;
    while (pos < r.out.size()) {
        size_t end = r.out.find('\n', pos);
        if (end == std::string::npos) end = r.out.size();
        std::string line = util::trim(r.out.substr(pos, end - pos));
        pos = end + 1;
        const std::string prefix = "package:";
        if (util::startsWith(line, prefix)) {
            AppItem item;
            item.packageName = line.substr(prefix.size());
            if (!item.packageName.empty()) apps.push_back(item);
        }
    }
    return true;
}

bool MobileUseProvider::installApp(Backend b, const std::string& downloadUrl,
                                   std::string& err) {
    const std::string localPath = "/data/local/tmp/mcp_mobile_use_install.apk";
    ExecResult r = executor(b)->runShell(cmd::installApk(downloadUrl, localPath), 300000);
    if (!r.ok()) {
        err = "install app failed: " + r.describe();
        return false;
    }
    if (r.out.find("Success") == std::string::npos && r.out.find("success") == std::string::npos) {
        err = "install app failed: " + util::trim(r.out);
        return false;
    }
    return true;
}

}  // namespace service
