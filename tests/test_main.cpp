#include <cstdio>
#include <cstring>
#include <string>

#include "json/json.hpp"
#include "mcp/jwt.hpp"
#include "service/command_builder.hpp"
#include "util/sha256.hpp"
#include "util/util.hpp"

namespace {

int gFailures = 0;
int gChecks = 0;

void check(bool cond, const char* expr, int line) {
    gChecks++;
    if (!cond) {
        gFailures++;
        printf("FAIL line %d: %s\n", line, expr);
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

void testJsonBasic() {
    mj::Value v = mj::Value::parse(R"({"a":1,"b":"x","c":[1,2.5,true,null],"d":{"e":"f"}})");
    CHECK(v.isObject());
    CHECK(v["a"].asInt() == 1);
    CHECK(v["b"].asString() == "x");
    CHECK(v["c"].asArray().size() == 4);
    CHECK(v["c"].asArray()[1].asNumber() == 2.5);
    CHECK(v["c"].asArray()[2].asBool() == true);
    CHECK(v["c"].asArray()[3].isNull());
    CHECK(v["d"]["e"].asString() == "f");
    CHECK(v.at("missing").isNull());
    CHECK(!v.has("missing"));
    CHECK(v.has("a"));
}

void testJsonEscapes() {
    mj::Value v = mj::Value::parse("\"a\\nb\\t\\\"c\\\\ \\u4e2d\\u6587\"");
    CHECK(v.asString() == "a\nb\t\"c\\ \xe4\xb8\xad\xe6\x96\x87");

    mj::Value obj = mj::Value::object();
    obj["k"] = "line1\nline2\"quoted\"";
    std::string dumped = obj.dump();
    mj::Value back = mj::Value::parse(dumped);
    CHECK(back["k"].asString() == obj["k"].asString());
}

void testJsonNumbers() {
    mj::Value v = mj::Value::parse("[-1, 0, 42, 3.14, 1e3]");
    CHECK(v.asArray()[0].asInt() == -1);
    CHECK(v.asArray()[1].asInt() == 0);
    CHECK(v.asArray()[2].asInt() == 42);
    CHECK(v.asArray()[3].asNumber() > 3.13 && v.asArray()[3].asNumber() < 3.15);
    CHECK(v.asArray()[4].asInt() == 1000);

    mj::Value n = 42;
    CHECK(n.dump() == "42");
    mj::Value d = 2.5;
    CHECK(d.dump() == "2.5");
}

void testJsonDumpStructure() {
    mj::Value obj = mj::Value::object();
    obj["x"] = 1;
    obj["y"] = "s";
    mj::Value arr = mj::Value::array();
    arr.push(1);
    arr.push("two");
    obj["z"] = arr;
    mj::Value back = mj::Value::parse(obj.dump());
    CHECK(back["x"].asInt() == 1);
    CHECK(back["y"].asString() == "s");
    CHECK(back["z"].asArray()[1].asString() == "two");
}

void testJsonErrors() {
    bool threw = false;
    try {
        mj::Value::parse("{bad");
    } catch (const mj::ParseError&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        mj::Value::parse("{\"a\":1} trailing");
    } catch (const mj::ParseError&) {
        threw = true;
    }
    CHECK(threw);
}

void testBase64() {
    CHECK(util::base64Encode("") == "");
    CHECK(util::base64Encode("f") == "Zg==");
    CHECK(util::base64Encode("fo") == "Zm8=");
    CHECK(util::base64Encode("foo") == "Zm9v");
    CHECK(util::base64Encode("foobar") == "Zm9vYmFy");

    std::string out;
    CHECK(util::base64Decode("Zm9vYmFy", out) && out == "foobar");
    CHECK(util::base64Decode("Zg==", out) && out == "f");

    std::string binary;
    for (int i = 0; i < 256; ++i) binary += static_cast<char>(i);
    std::string enc = util::base64Encode(binary);
    std::string dec;
    CHECK(util::base64Decode(enc, dec));
    CHECK(dec == binary);
}

void testShellQuote() {
    CHECK(util::shellQuote("abc") == "'abc'");
    CHECK(util::shellQuote("a'b") == "'a'\\''b'");
    CHECK(util::shellQuote("") == "''");
}

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
    CHECK(installApk("http://x/a.apk", "/data/local/tmp/a.apk").find("pm install") !=
          std::string::npos);
    CHECK(isAsciiPrintable("abc123"));
    CHECK(!isAsciiPrintable("中文"));
    CHECK(!isAsciiPrintable("a\nb"));
}

void testSha256Hmac() {
    CHECK(util::sha256Hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(util::sha256Hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(util::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    std::string key = "key";
    std::string msg = "The quick brown fox jumps over the lazy dog";
    std::string mac = util::hmacSha256(key, msg);
    static const char* kHex = "0123456789abcdef";
    std::string macHex;
    for (unsigned char c : mac) {
        macHex += kHex[c >> 4];
        macHex += kHex[c & 0xF];
    }
    CHECK(macHex == "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
}

void testJwt() {
    // 手工构造 HS256 token
    std::string header = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    std::string payload = "{\"sub\":\"e2e\",\"exp\":4102444800}";
    std::string signingInput =
        util::base64UrlEncode(reinterpret_cast<const unsigned char*>(header.data()),
                              header.size()) +
        "." + util::base64UrlEncode(reinterpret_cast<const unsigned char*>(payload.data()),
                                    payload.size());
    std::string secret = "test-secret";
    std::string sig = util::base64UrlEncode(
        reinterpret_cast<const unsigned char*>(util::hmacSha256(secret, signingInput).data()),
        32);

    mcp::JwtVerifier v;
    v.setHs256Secret(secret);
    CHECK(v.enabled());
    CHECK(v.verify(signingInput + "." + sig));

    mcp::JwtVerifier wrong;
    wrong.setHs256Secret("other-secret");
    CHECK(!wrong.verify(signingInput + "." + sig));

    // 篡改 payload
    std::string tampered = signingInput.substr(0, signingInput.rfind('.')) + "X." + sig;
    CHECK(!v.verify(tampered));

    // 过期 token
    std::string expiredPayload = "{\"sub\":\"e2e\",\"exp\":1}";
    std::string expInput =
        util::base64UrlEncode(reinterpret_cast<const unsigned char*>(header.data()),
                              header.size()) +
        "." + util::base64UrlEncode(
                  reinterpret_cast<const unsigned char*>(expiredPayload.data()),
                  expiredPayload.size());
    std::string expSig = util::base64UrlEncode(
        reinterpret_cast<const unsigned char*>(util::hmacSha256(secret, expInput).data()), 32);
    CHECK(!v.verify(expInput + "." + expSig));
}

void testBase64Url() {
    std::string out;
    CHECK(util::base64UrlDecode("SGVsbG8", out) && out == "Hello");
    std::string enc = util::base64UrlEncode(reinterpret_cast<const unsigned char*>("a.b/c+d"),
                                            std::string("a.b/c+d").size());
    CHECK(util::base64UrlDecode(enc, out) && out == "a.b/c+d");
}

}  // namespace

int main() {
    testJsonBasic();
    testJsonEscapes();
    testJsonNumbers();
    testJsonDumpStructure();
    testJsonErrors();
    testBase64();
    testShellQuote();
    testCommandBuilder();
    testSha256Hmac();
    testBase64Url();
    testJwt();

    printf("%d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
