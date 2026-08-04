#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mj {

class Value {
public:
    enum Type { Null, Bool, Number, String, Array, Object };
    using Arr = std::vector<Value>;
    using Pair = std::pair<std::string, Value>;
    using Obj = std::vector<Pair>;

    Value() : type_(Null), b_(false), n_(0) {}
    Value(std::nullptr_t) : Value() {}
    Value(bool b) : type_(Bool), b_(b), n_(0) {}
    Value(int i) : type_(Number), b_(false), n_(i) {}
    Value(long l) : type_(Number), b_(false), n_(static_cast<double>(l)) {}
    Value(long long l) : type_(Number), b_(false), n_(static_cast<double>(l)) {}
    Value(double d) : type_(Number), b_(false), n_(d) {}
    Value(const char* s) : type_(String), b_(false), n_(0), s_(s ? s : "") {}
    Value(const std::string& s) : type_(String), b_(false), n_(0), s_(s) {}
    Value(const Arr& a) : type_(Array), b_(false), n_(0), a_(a) {}
    Value(const Obj& o) : type_(Object), b_(false), n_(0), o_(o) {}

    static Value array() { return Value(Arr{}); }
    static Value object() { return Value(Obj{}); }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Null; }
    bool isBool() const { return type_ == Bool; }
    bool isNumber() const { return type_ == Number; }
    bool isString() const { return type_ == String; }
    bool isArray() const { return type_ == Array; }
    bool isObject() const { return type_ == Object; }

    bool asBool(bool def = false) const {
        if (type_ == Bool) return b_;
        if (type_ == Number) return n_ != 0;
        return def;
    }
    double asNumber(double def = 0) const { return type_ == Number ? n_ : def; }
    int asInt(int def = 0) const { return type_ == Number ? static_cast<int>(n_) : def; }
    long long asInt64(long long def = 0) const {
        return type_ == Number ? static_cast<long long>(n_) : def;
    }
    std::string asString(const std::string& def = "") const {
        return type_ == String ? s_ : def;
    }
    const Arr& asArray() const {
        static const Arr kEmpty;
        return type_ == Array ? a_ : kEmpty;
    }
    const Obj& asObject() const {
        static const Obj kEmpty;
        return type_ == Object ? o_ : kEmpty;
    }

    size_t size() const {
        if (type_ == Array) return a_.size();
        if (type_ == Object) return o_.size();
        if (type_ == String) return s_.size();
        return 0;
    }

    bool has(const std::string& key) const {
        if (type_ != Object) return false;
        for (const auto& p : o_)
            if (p.first == key) return true;
        return false;
    }

    const Value& at(const std::string& key) const {
        static const Value kNull;
        if (type_ != Object) return kNull;
        for (const auto& p : o_)
            if (p.first == key) return p.second;
        return kNull;
    }

    const Value& operator[](const std::string& key) const { return at(key); }

    Value& operator[](const std::string& key) {
        if (type_ != Object) {
            type_ = Object;
            o_.clear();
        }
        for (auto& p : o_)
            if (p.first == key) return p.second;
        o_.emplace_back(key, Value());
        return o_.back().second;
    }

    void push(const Value& v) {
        if (type_ != Array) {
            type_ = Array;
            a_.clear();
        }
        a_.push_back(v);
    }

    std::string dump() const;

    static Value parse(const std::string& text);

private:
    Type type_;
    bool b_;
    double n_;
    std::string s_;
    Arr a_;
    Obj o_;
};

class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& msg) : std::runtime_error(msg) {}
};

}  // namespace mj
