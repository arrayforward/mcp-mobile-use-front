#include "command_builder.hpp"

#include "../util/util.hpp"

namespace service {
namespace cmd {

std::string tap(int x, int y) {
    return "input tap " + std::to_string(x) + " " + std::to_string(y);
}

std::string swipe(int x1, int y1, int x2, int y2, int durationMs) {
    return "input swipe " + std::to_string(x1) + " " + std::to_string(y1) + " " +
           std::to_string(x2) + " " + std::to_string(y2) + " " + std::to_string(durationMs);
}

std::string keyEvent(int keyCode) {
    return "input keyevent " + std::to_string(keyCode);
}

std::string screenshotStdout() {
    return "screencap -p";
}

std::string screenshotToFile(const std::string& path) {
    return "screencap -p " + util::shellQuote(path);
}

std::string removeFile(const std::string& path) {
    return "rm -f " + util::shellQuote(path);
}

std::string screenSize() {
    return "wm size";
}

std::string textInputSelectIme() {
    return "settings put secure default_input_method "
           "'com.android.inputmethod.pinyin/.PinyinIME'";
}

std::string textInputClear() {
    return "am broadcast -a device.gameservice.keyevent.clear";
}

std::string textInputBroadcast(const std::string& text) {
    return "am broadcast -a device.gameservice.keyevent.value --es value " +
           util::shellQuote(text);
}

std::string inputTextDirect(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case ' ': escaped += "%s"; break;
            case '%': escaped += "%25"; break;
            default: escaped += c;
        }
    }
    return "input text " + util::shellQuote(escaped);
}

std::string launchApp(const std::string& packageName) {
    return "monkey -p " + util::shellQuote(packageName) +
           " -c android.intent.category.LAUNCHER 1 2>&1 | grep -q . ; "
           "am start -n \"$(cmd package resolve-activity --brief " +
           util::shellQuote(packageName) +
           " | tail -n 1)\" 2>/dev/null || monkey -p " + util::shellQuote(packageName) +
           " -c android.intent.category.LAUNCHER 1";
}

std::string closeApp(const std::string& packageName) {
    return "am force-stop " + util::shellQuote(packageName);
}

std::string listPackages(bool thirdPartyOnly) {
    return thirdPartyOnly ? "pm list packages -3" : "pm list packages";
}

std::string installApk(const std::string& downloadUrl, const std::string& localPath) {
    std::string path = util::shellQuote(localPath);
    std::string url = util::shellQuote(downloadUrl);
    return "(command -v curl >/dev/null 2>&1 && curl -fSL -o " + path + " " + url + ") || "
           "(command -v wget >/dev/null 2>&1 && wget -O " + path + " " + url + "); "
           "rc=$?; if [ $rc -ne 0 ]; then rm -f " + path + "; echo 'download failed'; exit $rc; fi; "
           "pm install -r " + path + "; rc=$?; rm -f " + path + "; exit $rc";
}

bool isAsciiPrintable(const std::string& text) {
    for (unsigned char c : text) {
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

}  // namespace cmd
}  // namespace service
