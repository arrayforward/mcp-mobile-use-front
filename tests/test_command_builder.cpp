#include "test_framework.hpp"

#include "service/command_builder.hpp"

namespace {

void testCommandBuilder() {
    using namespace service::cmd;
    CHECK(tap(100, 200) == "input tap 100 200");
    CHECK(swipe(1, 2, 3, 4, 300) == "input swipe 1 2 3 4 300");
    CHECK(keyEvent(4) == "input keyevent 4");
    CHECK(screenshotStdout() == "screencap -p");
    CHECK(screenSize() == "wm size");
    CHECK(closeApp("com.x") == "am force-stop 'com.x'");
    CHECK(listPackages(true) == "pm list packages -3");
    CHECK(listPackages(false) == "pm list packages");
    CHECK(inputTextDirect("hello world") == "input text 'hello%sworld'");
    CHECK(inputTextDirect("100%") == "input text '100%25'");
    CHECK(textInputBroadcast("中文").find("device.gameservice.keyevent.value") !=
          std::string::npos);
    CHECK(textInputSelectIme().find("default_input_method") != std::string::npos);
    CHECK(installApk("http://x/a.apk", "/data/local/tmp/a.apk").find("pm install") !=
          std::string::npos);
    CHECK(isAsciiPrintable("abc123"));
    CHECK(!isAsciiPrintable("中文"));
    CHECK(!isAsciiPrintable("a\nb"));
}

}  // namespace

void runCommandBuilderTests() {
    testCommandBuilder();
}
