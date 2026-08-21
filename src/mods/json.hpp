#pragma once
#include "core/types.hpp"
#include <variant>
#include <map>
#include <charconv>
#include <cstring>
#include <cctype>
#include <stdexcept>

namespace vw::json {

struct Value;
using Object = std::map<std::string, Value, std::less<>>;
using Array  = std::vector<Value>;

struct Value {
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> v = nullptr;

    bool is_object() const { return std::holds_alternative<Object>(v); }
    bool is_array()  const { return std::holds_alternative<Array>(v); }
    bool is_string() const { return std::holds_alternative<std::string>(v); }

    const Object* obj() const { return std::get_if<Object>(&v); }
    const Array*  arr() const { return std::get_if<Array>(&v); }
    const std::string* str() const { return std::get_if<std::string>(&v); }
    double num(double def = 0) const { auto* d = std::get_if<double>(&v); return d ? *d : def; }

    const Value* get(std::string_view key) const {
        const auto* o = obj();
        if (!o) return nullptr;
        auto it = o->find(key);
        return it == o->end() ? nullptr : &it->second;
    }
};

class Parser {
public:
    explicit Parser(std::string_view s) : p_(s.data()), end_(s.data() + s.size()) {}

    Value parse() {
        Value v = value();
        ws();
        if (p_ != end_) fail("trailing characters after document");
        return v;
    }

private:
    [[noreturn]] void fail(const char* msg) { throw std::runtime_error(std::string("json: ") + msg); }
    void ws() { while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_; }
    char peek() { ws(); if (p_ >= end_) fail("EOF"); return *p_; }

    Value value() {
        switch (peek()) {
            case '{': return object();
            case '[': return array();
            case '"': return Value{string()};
            case 't': lit("true");  return Value{true};
            case 'f': lit("false"); return Value{false};
            case 'n': lit("null");  return Value{nullptr};
            default:  return number();
        }
    }
    void lit(const char* s) {
        const size_t n = std::strlen(s);
        if (static_cast<size_t>(end_ - p_) < n || std::memcmp(p_, s, n) != 0) fail("bad literal");
        p_ += n;
    }
    Value number() {
        const char* s = p_;
        if (p_ < end_ && (*p_ == '-' || *p_ == '+')) ++p_;
        while (p_ < end_ && (std::isdigit(static_cast<u8>(*p_)) || *p_ == '.' ||
                             *p_ == 'e' || *p_ == 'E' || *p_ == '-' || *p_ == '+')) ++p_;
        double d = 0;
        auto [ptr, ec] = std::from_chars(s, p_, d);
        if (ec != std::errc{} || ptr != p_) fail("bad number");
        return Value{d};
    }
    std::string string() {
        if (*p_ != '"') fail("expected string");
        ++p_;
        std::string s;
        while (p_ < end_ && *p_ != '"') {
            if (*p_ == '\\' && p_ + 1 < end_) {
                ++p_;
                switch (*p_) {
                    case 'n': s += '\n'; break; case 't': s += '\t'; break;
                    case 'r': s += '\r'; break; case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case 'u': {  // \uXXXX -> UTF-8 (без суррогатных пар — достаточно для ассетов)
                        if (p_ + 4 >= end_) fail("bad \\u");
                        u32 cp = 0;
                        for (int i = 1; i <= 4; ++i) {
                            const char c = p_[i];
                            cp <<= 4;
                            if (c >= '0' && c <= '9') cp |= u32(c - '0');
                            else if (c >= 'a' && c <= 'f') cp |= u32(c - 'a' + 10);
                            else if (c >= 'A' && c <= 'F') cp |= u32(c - 'A' + 10);
                            else fail("bad \\u");
                        }
                        p_ += 4;
                        if (cp < 0x80) s += static_cast<char>(cp);
                        else if (cp < 0x800) {
                            s += static_cast<char>(0xC0 | (cp >> 6));
                            s += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            s += static_cast<char>(0xE0 | (cp >> 12));
                            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            s += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: s += *p_;
                }
            } else {
                s += *p_;
            }
            ++p_;
        }
        if (p_ >= end_) fail("unterminated string");
        ++p_;
        return s;
    }
    // Лимит глубины вложенности: специально созданный JSON из JAR
    // не должен переполнять стек рекурсией object()/array().
    static constexpr int kMaxDepth = 256;

    Value object() {
        ++p_;  // '{'
        if (++depth_ > kMaxDepth) fail("nesting too deep");
        Object o;
        if (peek() == '}') { ++p_; --depth_; return Value{std::move(o)}; }
        while (true) {
            ws();
            std::string k = string();
            if (peek() != ':') fail("expected ':'");
            ++p_;
            o.emplace(std::move(k), value());
            const char c = peek();
            if (c == ',') { ++p_; continue; }
            if (c == '}') { ++p_; --depth_; break; }
            fail("expected ',' or '}'");
        }
        return Value{std::move(o)};
    }
    Value array() {
        ++p_;  // '['
        if (++depth_ > kMaxDepth) fail("nesting too deep");
        Array a;
        if (peek() == ']') { ++p_; --depth_; return Value{std::move(a)}; }
        while (true) {
            a.push_back(value());
            const char c = peek();
            if (c == ',') { ++p_; continue; }
            if (c == ']') { ++p_; --depth_; break; }
            fail("expected ',' or ']'");
        }
        return Value{std::move(a)};
    }

    const char* p_;
    const char* end_;
    int depth_ = 0;
};

inline Value parse(std::string_view s) { return Parser(s).parse(); }
inline Value parse(std::span<const u8> b) {
    return parse(std::string_view(reinterpret_cast<const char*>(b.data()), b.size()));
}

} // namespace vw::json
