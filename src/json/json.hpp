/**
 * @file json.hpp
 * @brief 手写 JSON 库（mj::Value）——零第三方依赖的核心基础组件
 *
 * 功能：
 *   提供 JSON 文档模型的解析（parse）与序列化（dump），支持 null/bool/数字/字符串/
 *   数组/对象六种类型，对象保留键插入顺序，支持 UTF-8 与 \uXXXX 转义（含代理对）。
 *
 * 开发思路：
 *   1. 为满足"零依赖全手写"约束，不引入 nlohmann/json，手写递归下降解析器
 *      （见 json.cpp 中 Parser 结构体）与递归序列化器。
 *   2. 对象采用 std::vector<std::pair<key,Value>> 有序存储：MCP 工具 schema 与
 *      响应需要保持字段顺序（如 tools/list 按注册顺序返回），故放弃无序 map。
 *   3. 数字统一以 double 存储，dump 时对整数值输出无小数点格式，避免
 *      "42.000000" 之类的噪音，便于 LLM 阅读与测试断言。
 *   4. 非 const operator[] 会惰性插入键（与 nlohmann 一致），只读场景请用 at()。
 *
 * @author hubin
 * @date 2026-08-05
 */
#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mj {

/**
 * @class Value
 * @brief JSON 值类型（variant 语义：同一时刻仅一种类型有效）
 *
 * 开发思路：
 *   用单一类承载全部类型，内部以 Type 枚举 + 各成员（m_b/m_n/m_s/m_a/m_o）区分，
 *   避免继承体系带来的拷贝/赋值复杂度；提供 asXxx(def) 安全取值接口，
 *   类型不匹配时返回默认值而非抛异常，方便协议层容错。
 *
 * @author hubin
 * @date 2026-08-05
 */
class Value {
public:
    enum Type { Null, Bool, Number, String, Array, Object };
    using Arr = std::vector<Value>;
    using Pair = std::pair<std::string, Value>;
    using Obj = std::vector<Pair>;

    Value() : m_type(Null), m_b(false), m_n(0) {}
    Value(std::nullptr_t) : Value() {}
    Value(bool b) : m_type(Bool), m_b(b), m_n(0) {}
    Value(int i) : m_type(Number), m_b(false), m_n(i) {}
    Value(long l) : m_type(Number), m_b(false), m_n(static_cast<double>(l)) {}
    Value(long long l) : m_type(Number), m_b(false), m_n(static_cast<double>(l)) {}
    Value(double d) : m_type(Number), m_b(false), m_n(d) {}
    Value(const char* s) : m_type(String), m_b(false), m_n(0), m_s(s ? s : "") {}
    Value(const std::string& s) : m_type(String), m_b(false), m_n(0), m_s(s) {}
    Value(const Arr& a) : m_type(Array), m_b(false), m_n(0), m_a(a) {}
    Value(const Obj& o) : m_type(Object), m_b(false), m_n(0), m_o(o) {}

    /** @brief 构造空数组 */
    static Value array() { return Value(Arr{}); }
    /** @brief 构造空对象 */
    static Value object() { return Value(Obj{}); }

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Null; }
    bool isBool() const { return m_type == Bool; }
    bool isNumber() const { return m_type == Number; }
    bool isString() const { return m_type == String; }
    bool isArray() const { return m_type == Array; }
    bool isObject() const { return m_type == Object; }

    /**
     * @brief 安全取 bool，类型不匹配返回默认值
     * @param def 默认值
     */
    bool asBool(bool def = false) const {
        if (m_type == Bool) return m_b;
        if (m_type == Number) return m_n != 0;
        return def;
    }
    /**
     * @brief 安全取 double
     * @param def 默认值
     */
    double asNumber(double def = 0) const { return m_type == Number ? m_n : def; }
    /**
     * @brief 安全取 int（内部为 double，可能截断）
     * @param def 默认值
     */
    int asInt(int def = 0) const { return m_type == Number ? static_cast<int>(m_n) : def; }
    /**
     * @brief 安全取 int64
     * @param def 默认值
     */
    long long asInt64(long long def = 0) const {
        return m_type == Number ? static_cast<long long>(m_n) : def;
    }
    /**
     * @brief 安全取 string
     * @param def 默认值
     */
    std::string asString(const std::string& def = "") const {
        return m_type == String ? m_s : def;
    }
    /** @brief 安全取数组（非数组返回空数组引用） */
    const Arr& asArray() const {
        static const Arr kEmpty;
        return m_type == Array ? m_a : kEmpty;
    }
    /** @brief 安全取对象（非对象返回空对象引用） */
    const Obj& asObject() const {
        static const Obj kEmpty;
        return m_type == Object ? m_o : kEmpty;
    }

    /** @brief 元素个数（数组/对象/字符串） */
    size_t size() const {
        if (m_type == Array) return m_a.size();
        if (m_type == Object) return m_o.size();
        if (m_type == String) return m_s.size();
        return 0;
    }

    /** @brief 对象是否包含指定键 */
    bool has(const std::string& key) const {
        if (m_type != Object) return false;
        for (const auto& p : m_o)
            if (p.first == key) return true;
        return false;
    }

    /**
     * @brief 只读取键值，键不存在返回 null 值（不插入）
     * @param key 键名
     */
    const Value& at(const std::string& key) const {
        static const Value kNull;
        if (m_type != Object) return kNull;
        for (const auto& p : m_o)
            if (p.first == key) return p.second;
        return kNull;
    }

    /** @brief 只读下标访问（等价 at()，不插入） */
    const Value& operator[](const std::string& key) const { return at(key); }

    /**
     * @brief 可写下标访问：键不存在时插入新键并返回引用（惰性插入）
     * @param key 键名
     */
    Value& operator[](const std::string& key) {
        if (m_type != Object) {
            m_type = Object;
            m_o.clear();
        }
        for (auto& p : m_o)
            if (p.first == key) return p.second;
        m_o.emplace_back(key, Value());
        return m_o.back().second;
    }

    /** @brief 数组追加元素（非数组时先转换为数组） */
    void push(const Value& v) {
        if (m_type != Array) {
            m_type = Array;
            m_a.clear();
        }
        m_a.push_back(v);
    }

    /**
     * @brief 序列化为 JSON 字符串
     *
     * 实现思路：递归 dumpValue，字符串按 JSON 规范转义
     * （" \ / \b \f \n \r \t 及控制字符 \u00XX），UTF-8 原样输出。
     * 伪代码：
     *   dumpValue(v):
     *     switch v.type:
     *       Null -> "null"; Bool -> true/false
     *       Number -> 整数值用 %lld 无小数点，否则 %.17g
     *       String -> dumpString（转义后加引号）
     *       Array -> [ elem, elem, ... ]
     *       Object -> { "k": v, ... }（按插入顺序）
     */
    std::string dump() const;

    /**
     * @brief 解析 JSON 文本
     * @param text 输入文本
     * @return 解析结果 Value
     * @throws ParseError 语法错误时抛出（含位置信息）
     *
     * 实现思路：递归下降解析，skipWs 跳过空白；字符串支持 \uXXXX 及代理对
     * （高代理后紧跟 \u 低代理时合并为 4 字节 UTF-8）。
     */
    static Value parse(const std::string& text);

private:
    Type m_type;
    bool m_b;
    double m_n;
    std::string m_s;
    Arr m_a;
    Obj m_o;
};

/**
 * @class ParseError
 * @brief JSON 解析错误异常（继承 runtime_error，消息含出错位置）
 * @author hubin
 * @date 2026-08-05
 */
class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& msg) : std::runtime_error(msg) {}
};

}  // namespace mj
