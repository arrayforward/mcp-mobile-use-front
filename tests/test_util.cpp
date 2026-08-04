#include "test_framework.hpp"

#include "util/sha256.hpp"
#include "util/util.hpp"

namespace {

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

void testBase64Url() {
    std::string out;
    CHECK(util::base64UrlDecode("SGVsbG8", out) && out == "Hello");
    std::string enc = util::base64UrlEncode(reinterpret_cast<const unsigned char*>("a.b/c+d"),
                                            std::string("a.b/c+d").size());
    CHECK(util::base64UrlDecode(enc, out) && out == "a.b/c+d");
}

void testSha256Hmac() {
    // NIST / FIPS 180-4 标准向量
    CHECK(util::sha256Hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(util::sha256Hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(util::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // HMAC RFC 2202 向量（key="key", fox 消息）
    std::string mac = util::hmacSha256("key", "The quick brown fox jumps over the lazy dog");
    static const char* kHex = "0123456789abcdef";
    std::string macHex;
    for (unsigned char c : mac) {
        macHex += kHex[c >> 4];
        macHex += kHex[c & 0xF];
    }
    CHECK(macHex == "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
}

}  // namespace

void runUtilTests() {
    testBase64();
    testShellQuote();
    testBase64Url();
    testSha256Hmac();
}
