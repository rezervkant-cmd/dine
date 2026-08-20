#pragma once
#include "core/types.hpp"
#include <variant>
#include <memory>
#include <map>
#include <stdexcept>

namespace vw::nbt {

enum class TagType : u8 {
    End = 0, Byte, Short, Int, Long, Float, Double,
    ByteArray, String, List, Compound, IntArray, LongArray
};

struct Tag;
using TagPtr   = std::unique_ptr<Tag>;
using Compound = std::map<std::string, Tag, std::less<>>;
struct List { TagType elem = TagType::End; std::vector<Tag> items; };

struct Tag {
    std::variant<std::monostate, i8, i16, i32, i64, f32, f64,
                 std::vector<i8>, std::string, List, Compound,
                 std::vector<i32>, std::vector<i64>> v;

    TagType type() const { return static_cast<TagType>(v.index()); }

    template <class T> const T* as() const { return std::get_if<T>(&v); }

    i64 as_int(i64 def = 0) const {
        switch (type()) {
            case TagType::Byte:  return std::get<i8>(v);
            case TagType::Short: return std::get<i16>(v);
            case TagType::Int:   return std::get<i32>(v);
            case TagType::Long:  return std::get<i64>(v);
            default: return def;
        }
    }
    f64 as_double(f64 def = 0) const {
        if (type() == TagType::Float)  return std::get<f32>(v);
        if (type() == TagType::Double) return std::get<f64>(v);
        return static_cast<f64>(as_int(static_cast<i64>(def)));
    }
    const std::string* as_string() const { return as<std::string>(); }
    const Compound*    as_compound() const { return as<Compound>(); }
    const List*        as_list() const { return as<List>(); }

    const Tag* find(std::string_view path) const;
};

struct Document {
    std::string root_name;
    Tag root;
};

class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Document parse(std::span<const u8> data);

Document parse_auto(std::span<const u8> data);

std::vector<u8> inflate(std::span<const u8> data, bool gzip_header);

std::string to_snbt(const Tag& t, int indent = 0);

}
