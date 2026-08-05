/**
 * @file json.cpp
 * @brief 手写 JSON 库实现——序列化（dump）与递归下降解析（parse）
 *
 * 功能：
 *   实现 mj::Value 的 dump()/parse()：dump 递归输出最小化 JSON 文本，
 *   parse 使用内部 Parser 结构体做递归下降解析，错误带行列位置信息。
 *
 * 开发思路：
 *   1. Parser 持有文本指针与游标 m_pos，peek/next 逐字符推进，
 *      按 JSON 文法 value -> object/array/string/number/literal 递归。
 *   2. 字符串解析支持标准转义与 \uXXXX；遇到高代理(D800-DBFF)时向后
 *      探测 \uDC00-DFFF 低代理并合并为 4 字节 UTF-8，保证 emoji 正确。
 *   3. dumpString 对控制字符统一输出 \u00XX，非 ASCII UTF-8 原样透传，
 *      保证中文内容可读、测试断言稳定。
 *
 * @author hubin
 * @date 2026-08-05
 */
#include "json/json.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>

namespace mj {

namespace {

/**
 * @brief 十六进制数字字符转数值
 * @param c '0'-'9' 'a'-'f' 'A'-'F'
 * @return 0-15，非法字符返回 -1
 */
int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * @brief 将 Unicode 码点编码为 UTF-8 追加到输出
 * @param cp Unicode 码点（U+0000..U+10FFFF）
 * @param out 输出缓冲
 *
 * 实现思路：按码点范围选择 1/2/3/4 字节编码，逐段拼接首字节+续字节(10xxxxxx)。
 */
void appendUtf8(unsigned cp, std::string& out) {
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

/**
 * @struct Parser
 * @brief 递归下降 JSON 解析器（内部实现，单次解析即弃）
 *
 * 开发思路：游标式顺序扫描；每个 parseXxx 消费一个完整语法单元并推进 m_pos，
 * 出错直接抛 ParseError（携带 pos 便于定位），不做错误恢复。
 *
 * @author hubin
 * @date 2026-08-05
 */
struct Parser {
    const std::string& s;
    size_t m_pos = 0;

    explicit Parser(const std::string& src) : s(src) {}

    /** @brief 当前字符（越界返回 '\0'） */
    char peek() const { return m_pos < s.size() ? s[m_pos] : '\0'; }
    /** @brief 取当前字符并推进游标 */
    char next() { return m_pos < s.size() ? s[m_pos++] : '\0'; }
    /** @brief 跳过空白（空格/制表/换行/回车） */
    void skipWs() {
        while (m_pos < s.size()) {
            char c = s[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++m_pos;
            else
                break;
        }
    }
    /** @brief 期望消费指定字符，否则抛错 */
    void expect(char c) {
        if (next() != c) error(std::string("expected '") + c + "'");
    }
    [[noreturn]] void error(const std::string& msg) const {
        throw ParseError("JSON parse error at offset " + std::to_string(m_pos) + ": " + msg);
    }

    /**
     * @brief 解析一个 JSON 值（入口，分派到各类型）
     * @return 解析出的 Value
     *
     * 伪代码：
     *   skipWs; c = peek
     *   '{' -> parseObject; '[' -> parseArray; '"' -> parseString
     *   't' -> true; 'f' -> false; 'n' -> null
     *   '-' 或数字 -> parseNumber; 否则报错
     */
    Value parseValue() {
        skipWs();
        char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return Value(parseString());
        if (c == 't') return parseLiteral("true", Value(true));
        if (c == 'f') return parseLiteral("false", Value(false));
        if (c == 'n') return parseLiteral("null", Value());
        if (c == '-' || (c >= '0' && c <= '9')) return Value(parseNumber());
        error("unexpected character");
    }

    /**
     * @brief 匹配字面量 true/false/null
     * @param lit 字面量文本
     * @param v 对应 Value
     */
    Value parseLiteral(const char* lit, const Value& v) {
        for (const char* p = lit; *p; ++p)
            if (next() != *p) error("invalid literal");
        return v;
    }

    /**
     * @brief 解析对象 {"k":v,...}，保留键插入顺序
     *
     * 伪代码：
     *   '{' skipWs; 若 '}' 返回空对象
     *   循环: 键=parseString; ':'; 值=parseValue; 追加 (k,v)
     *         ',' 继续; '}' 结束
     */
    Value parseObject() {
        expect('{');
        Value::Obj obj;
        skipWs();
        if (peek() == '}') {
            next();
            return Value(obj);
        }
        while (true) {
            skipWs();
            if (peek() != '"') error("expected string key");
            std::string key = parseString();
            skipWs();
            expect(':');
            Value v = parseValue();
            obj.emplace_back(std::move(key), std::move(v));
            skipWs();
            char c = next();
            if (c == ',') continue;
            if (c == '}') break;
            error("expected ',' or '}'");
        }
        return Value(obj);
    }

    /** @brief 解析数组 [v,v,...] */
    Value parseArray() {
        expect('[');
        Value::Arr arr;
        skipWs();
        if (peek() == ']') {
            next();
            return Value(arr);
        }
        while (true) {
            arr.push_back(parseValue());
            skipWs();
            char c = next();
            if (c == ',') continue;
            if (c == ']') break;
            error("expected ',' or ']'");
        }
        return Value(arr);
    }

    /**
     * @brief 解析字符串（含转义与 \uXXXX 代理对）
     * @return 解码后的 UTF-8 字符串
     *
     * 实现思路：逐字符扫描至未转义的 '"'；
     * '\\' 后按转义表处理；'u' 读 4 位 hex 得码点，
     * 若为高代理且后续是 \u 低代理则合并：
     *   cp = 0x10000 + ((hi-0xD800)<<10) + (lo-0xDC00)
     */
    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            if (m_pos >= s.size()) error("unterminated string");
            char c = next();
            if (c == '"') break;
            if (c == '\\') {
                char e = next();
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
                        unsigned cp = parseHex4();
                        if (cp >= 0xD800 && cp <= 0xDBFF) {
                            // 高代理：尝试合并紧随其后的 \uXXXX 低代理
                            size_t save = m_pos;
                            if (m_pos + 1 < s.size() && s[m_pos] == '\\' && s[m_pos + 1] == 'u') {
                                m_pos += 2;
                                unsigned lo = parseHex4();
                                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                } else {
                                    // 非法低代理：回退，单独输出高代理
                                    m_pos = save;
                                }
                            }
                        }
                        appendUtf8(cp, out);
                        break;
                    }
                    default: error("invalid escape");
                }
            } else {
                out += c;
            }
        }
        return out;
    }

    /** @brief 读取 4 位十六进制为码点 */
    unsigned parseHex4() {
        unsigned cp = 0;
        for (int i = 0; i < 4; ++i) {
            int hv = hexVal(next());
            if (hv < 0) error("invalid \\u escape");
            cp = (cp << 4) | static_cast<unsigned>(hv);
        }
        return cp;
    }

    /**
     * @brief 解析数字（JSON 文法：-?int(.frac)?([eE]±exp)?）
     * @return double 值
     */
    double parseNumber() {
        size_t start = m_pos;
        if (peek() == '-') next();
        while (std::isdigit(static_cast<unsigned char>(peek()))) next();
        if (peek() == '.') {
            next();
            while (std::isdigit(static_cast<unsigned char>(peek()))) next();
        }
        if (peek() == 'e' || peek() == 'E') {
            next();
            if (peek() == '+' || peek() == '-') next();
            while (std::isdigit(static_cast<unsigned char>(peek()))) next();
        }
        if (m_pos == start) error("invalid number");
        return std::stod(s.substr(start, m_pos - start));
    }
};

/**
 * @brief 字符串转义序列化（加引号）
 * @param s 原始字符串
 * @param out 输出缓冲
 */
void dumpString(const std::string& s, std::string& out) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;  // 非控制字符（含 UTF-8 多字节）原样输出
                }
        }
    }
    out += '"';
}

/**
 * @brief 递归序列化 Value 到输出缓冲
 * @param v 待序列化值
 * @param out 输出缓冲
 */
void dumpValue(const Value& v, std::string& out) {
    switch (v.type()) {
        case Value::Null: out += "null"; break;
        case Value::Bool: out += v.asBool() ? "true" : "false"; break;
        case Value::Number: {
            double d = v.asNumber();
            if (std::isfinite(d) && d == std::floor(d) && std::fabs(d) < 1e15) {
                // 整数值输出无小数点形式，避免 "42.000000" 噪音
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
        case Value::String: dumpString(v.asString(), out); break;
        case Value::Array: {
            out += '[';
            bool first = true;
            for (const auto& e : v.asArray()) {
                if (!first) out += ',';
                first = false;
                dumpValue(e, out);
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

}  // namespace

std::string Value::dump() const {
    std::string out;
    dumpValue(*this, out);
    return out;
}

Value Value::parse(const std::string& text) {
    Parser p(text);
    Value v = p.parseValue();
    p.skipWs();
    if (p.m_pos < text.size()) p.error("trailing characters");
    return v;
}

}  // namespace mj
