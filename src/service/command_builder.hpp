#pragma once

#include <string>

namespace service {
namespace cmd {

std::string tap(int x, int y);
std::string swipe(int x1, int y1, int x2, int y2, int durationMs);
std::string keyEvent(int keyCode);
std::string screenshotStdout();
std::string screenshotToFile(const std::string& path);
std::string removeFile(const std::string& path);
std::string screenSize();
std::string textInputSelectIme();
std::string textInputClear();
std::string textInputBroadcast(const std::string& text);
std::string inputTextDirect(const std::string& text);
std::string launchApp(const std::string& packageName);
std::string closeApp(const std::string& packageName);
std::string listPackages(bool thirdPartyOnly);
std::string installApk(const std::string& downloadUrl, const std::string& localPath);

bool isAsciiPrintable(const std::string& text);

}  // namespace cmd
}  // namespace service
