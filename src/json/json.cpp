#include "json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace mj {

namespace {

void dumpString(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

void dumpValue(const Value& v, std::string& out) {
    switch (v.type()) {
        case Value::Null:
            out += "null";
            break;
        case Value::Bool:
            out += v.asBool() ? "true" : "false";
            break;
        case Value::Number: {
            double d = v.asNumber();
            if (std::floor(d) == d && std::fabs(d) < 9007199254740992.0) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
                out += buf;
            } else {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.17g", d);
                out += buf;
            }
            break;
        }
        case Value::String:
            dumpString(v.asString(), out);
            break;
        case Value::Array: {
            out += '[';
            bool first = true;
            for (const auto& item : v.asArray()) {
                if (!first) out += ',';
                first = false;
                dumpValue(item, out);
            }
            out += ']';
            break;
        }
        case Value::Object: {
            out += '{';
            bool first = true;
            for (const auto& p : v.asObject()) {
                if (!first) out += ',';
                first = false;
                dumpString(p.first, out);
                out += ':';
                dumpValue(p.second, out);
            }
            out += '}';
            break;
        }
    }
}

struct Parser {
    const std::string& s;
    size_t pos = 0;

    explicit Parser(const std::string& text) : s(text) {}

    [[noreturn]] void fail(const std::string& msg) {
        throw ParseError("json parse error at " + std::to_string(pos) + ": " + msg);
    }

    void skipWs() {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
            pos++;
    }

    char peek() {
        if (pos >= s.size()) fail("unexpected end of input");
        return s[pos];
    }

    bool consume(char c) {
        if (pos < s.size() && s[pos] == c) {
            pos++;
            return true;
        }
        return false;
    }

    void expect(char c) {
        if (!consume(c)) fail(std::string("expected '") + c + "'");
    }

    void expectLiteral(const char* lit) {
        for (const char* p = lit; *p; ++p) {
            if (pos >= s.size() || s[pos] != *p) fail("invalid literal");
            pos++;
        }
    }

    Value parseValue() {
        skipWs();
        char c = peek();
        switch (c) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': return Value(parseString());
            case 't': expectLiteral("true"); return Value(true);
            case 'f': expectLiteral("false"); return Value(false);
            case 'n': expectLiteral("null"); return Value();
            default: return parseNumber();
        }
    }

    Value parseObject() {
        expect('{');
        Value obj = Value::object();
        skipWs();
        if (consume('}')) return obj;
        while (true) {
            skipWs();
            if (peek() != '"') fail("expected object key");
            std::string key = parseString();
            skipWs();
            expect(':');
            obj[key] = parseValue();
            skipWs();
            if (consume('}')) break;
            expect(',');
        }
        return obj;
    }

    Value parseArray() {
        expect('[');
        Value arr = Value::array();
        skipWs();
        if (consume(']')) return arr;
        while (true) {
            arr.push(parseValue());
            skipWs();
            if (consume(']')) break;
            expect(',');
        }
        return arr;
    }

    void appendUtf8(std::string& out, unsigned int cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    unsigned int parseHex4() {
        if (pos + 4 > s.size()) fail("bad \\u escape");
        unsigned int cp = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s[pos++];
            cp <<= 4;
            if (c >= '0' && c <= '9') cp |= c - '0';
            else if (c >= 'a' && c <= 'f') cp |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') cp |= c - 'A' + 10;
            else fail("bad \\u escape");
        }
        return cp;
    }

    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            if (pos >= s.size()) fail("unterminated string");
            char c = s[pos++];
            if (c == '"') break;
            if (c == '\\') {
                if (pos >= s.size()) fail("unterminated escape");
                char e = s[pos++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        unsigned int cp = parseHex4();
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            if (pos + 1 < s.size() && s[pos] == '\\' && s[pos + 1] == 'u') {
                                pos += 2;
                                unsigned int lo = parseHex4();
                                if (lo >= 0xDC00 && lo <= 0xDFFF)
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                else
                                    fail("bad surrogate pair");
                            } else {
                                fail("lone high surrogate");
                            }
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default: fail("bad escape");
                }
            } else {
                out += c;
            }
        }
        return out;
    }

    Value parseNumber() {
        size_t start = pos;
        if (pos < s.size() && s[pos] == '-') pos++;
        while (pos < s.size() &&
               ((s[pos] >= '0' && s[pos] <= '9') || s[pos] == '.' || s[pos] == 'e' ||
                s[pos] == 'E' || s[pos] == '+' || s[pos] == '-'))
            pos++;
        if (pos == start) fail("invalid value");
        return Value(std::strtod(s.substr(start, pos - start).c_str(), nullptr));
    }
};

}  // namespace

std::string Value::dump() const {
    std::string out;
    out.reserve(256);
    dumpValue(*this, out);
    return out;
}

Value Value::parse(const std::string& text) {
    Parser p(text);
    Value v = p.parseValue();
    p.skipWs();
    if (p.pos != text.size()) throw ParseError("trailing characters after json value");
    return v;
}

}  // namespace mj
