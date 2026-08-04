#include "test_framework.hpp"

#include "json/json.hpp"

namespace {

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

}  // namespace

void runJsonTests() {
    testJsonBasic();
    testJsonEscapes();
    testJsonNumbers();
    testJsonDumpStructure();
    testJsonErrors();
}
