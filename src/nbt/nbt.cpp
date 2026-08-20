#include "nbt.hpp"
#include <zlib.h>
#include <cstring>
#include <algorithm>
#include <array>
#include <bit>

namespace vw::nbt {

namespace {

constexpr i32 MAX_ARRAY = 8 * 1024 * 1024;
constexpr i32 MAX_LIST  = 1 * 1024 * 1024;

struct Reader {
    const u8* p;
    const u8* end;

    void need(size_t n) const {
        if (static_cast<size_t>(end - p) < n) throw ParseError("NBT: unexpected EOF");
    }
    u8 u8_() { need(1); return *p++; }
    template <class T> T be() {
        static_assert(std::is_trivially_copyable_v<T>);
        need(sizeof(T));
        T x;
        std::memcpy(&x, p, sizeof(T));
        p += sizeof(T);
        if constexpr (sizeof(T) > 1) {
            if constexpr (std::endian::native == std::endian::little) {
                auto bytes = std::bit_cast<std::array<u8, sizeof(T)>>(x);
                std::reverse(bytes.begin(), bytes.end());
                x = std::bit_cast<T>(bytes);
            }
        }
        return x;
    }
    std::string str() {
        u16 len = be<u16>();
        need(len);
        std::string s(reinterpret_cast<const char*>(p), len);
        p += len;
        return s;
    }
    size_t remaining() const { return static_cast<size_t>(end - p); }
};

Tag parse_payload(Reader& r, TagType t, int depth) {
    if (depth > 512) throw ParseError("NBT: nesting too deep");
    Tag out;
    switch (t) {
        case TagType::Byte:   out.v = r.be<i8>();  break;
        case TagType::Short:  out.v = r.be<i16>(); break;
        case TagType::Int:    out.v = r.be<i32>(); break;
        case TagType::Long:   out.v = r.be<i64>(); break;
        case TagType::Float:  out.v = std::bit_cast<f32>(r.be<u32>()); break;
        case TagType::Double: out.v = std::bit_cast<f64>(r.be<u64>()); break;
        case TagType::ByteArray: {
            i32 n = r.be<i32>();
            if (n < 0) throw ParseError("NBT: negative array size");
            if (n > MAX_ARRAY) throw ParseError("NBT: ByteArray too large");
            if (static_cast<size_t>(n) > r.remaining()) throw ParseError("NBT: ByteArray truncated");
            std::vector<i8> a(reinterpret_cast<const i8*>(r.p),
                              reinterpret_cast<const i8*>(r.p) + n);
            r.p += n;
            out.v = std::move(a);
            break;
        }
        case TagType::String: out.v = r.str(); break;
        case TagType::List: {
            List l;
            l.elem = static_cast<TagType>(r.u8_());
            if (l.elem > TagType::LongArray) throw ParseError("NBT: bad list element type");
            i32 n = r.be<i32>();
            if (n < 0) throw ParseError("NBT: negative list size");
            if (n > MAX_LIST) throw ParseError("NBT: list too large");
            if (n > 0 && static_cast<size_t>(n) > r.remaining() && l.elem != TagType::End)
                throw ParseError("NBT: list truncated");
            l.items.reserve(static_cast<size_t>(n));
            for (i32 i = 0; i < n; ++i)
                l.items.push_back(parse_payload(r, l.elem, depth + 1));
            out.v = std::move(l);
            break;
        }
        case TagType::Compound: {
            Compound c;
            while (true) {
                auto ct = static_cast<TagType>(r.u8_());
                if (ct == TagType::End) break;
                if (ct > TagType::LongArray) throw ParseError("NBT: bad tag type in compound");
                std::string name = r.str();
                c.emplace(std::move(name), parse_payload(r, ct, depth + 1));
                if (c.size() > 100000) throw ParseError("NBT: compound too large");
            }
            out.v = std::move(c);
            break;
        }
        case TagType::IntArray: {
            i32 n = r.be<i32>();
            if (n < 0) throw ParseError("NBT: negative array size");
            if (n > MAX_ARRAY) throw ParseError("NBT: IntArray too large");
            if (static_cast<size_t>(n) * sizeof(i32) > r.remaining())
                throw ParseError("NBT: IntArray truncated");
            std::vector<i32> a(static_cast<size_t>(n));
            for (auto& x : a) x = r.be<i32>();
            out.v = std::move(a);
            break;
        }
        case TagType::LongArray: {
            i32 n = r.be<i32>();
            if (n < 0) throw ParseError("NBT: negative array size");
            if (n > MAX_ARRAY) throw ParseError("NBT: LongArray too large");
            if (static_cast<size_t>(n) * sizeof(i64) > r.remaining())
                throw ParseError("NBT: LongArray truncated");
            std::vector<i64> a(static_cast<size_t>(n));
            for (auto& x : a) x = r.be<i64>();
            out.v = std::move(a);
            break;
        }
        default: throw ParseError("NBT: unknown tag type");
    }
    return out;
}

}

const Tag* Tag::find(std::string_view path) const {
    const Tag* cur = this;
    while (!path.empty()) {
        auto dot = path.find('.');
        std::string_view part = path.substr(0, dot);
        const auto* c = cur->as_compound();
        if (!c) return nullptr;
        auto it = c->find(part);
        if (it == c->end()) return nullptr;
        cur = &it->second;
        if (dot == std::string_view::npos) break;
        path.remove_prefix(dot + 1);
    }
    return cur;
}

Document parse(std::span<const u8> data) {
    Reader r{data.data(), data.data() + data.size()};
    auto t = static_cast<TagType>(r.u8_());
    if (t != TagType::Compound) throw ParseError("NBT: root is not a compound");
    Document doc;
    doc.root_name = r.str();
    doc.root = parse_payload(r, TagType::Compound, 0);
    return doc;
}

std::vector<u8> inflate(std::span<const u8> data, bool gzip_header) {
    std::vector<u8> out;
    constexpr size_t MAX_INFLATE = 64 * 1024 * 1024;
    out.reserve(std::min<size_t>(data.size() * 4, 1<<20));
    z_stream zs{};

    if (inflateInit2(&zs, gzip_header ? 15 + 32 : 15) != Z_OK)
        throw ParseError("zlib: inflateInit failed");
    zs.next_in  = const_cast<Bytef*>(data.data());
    zs.avail_in = static_cast<uInt>(data.size());
    u8 buf[64 * 1024];
    int rc;
    do {
        zs.next_out  = buf;
        zs.avail_out = sizeof(buf);
        rc = ::inflate(&zs, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) {
            inflateEnd(&zs);
            throw ParseError("zlib: inflate failed");
        }
        size_t have = sizeof(buf) - zs.avail_out;
        if (out.size() + have > MAX_INFLATE) {
            inflateEnd(&zs);
            throw ParseError("zlib: inflate too large");
        }
        out.insert(out.end(), buf, buf + have);
        // Поток закончился, но zlib не выдал Z_STREAM_END и больше не
        // потребляет вход — данные обрезаны.
        if (rc != Z_STREAM_END && zs.avail_in == 0 && have == 0) {
            inflateEnd(&zs);
            throw ParseError("zlib: truncated deflate stream");
        }
    } while (rc != Z_STREAM_END);
    inflateEnd(&zs);
    if (rc != Z_STREAM_END)
        throw ParseError("zlib: truncated deflate stream");
    return out;
}

Document parse_auto(std::span<const u8> data) {
    if (data.size() >= 2) {
        const bool gz = data[0] == 0x1f && data[1] == 0x8b;
        const bool zl = data[0] == 0x78;
        if (gz || zl) {
            auto raw = inflate(data, true);
            return parse(raw);
        }
    }
    return parse(data);
}

std::string to_snbt(const Tag& t, int indent) {
    std::string pad(static_cast<size_t>(indent) * 2, ' ');
    std::string s;
    switch (t.type()) {
        case TagType::Byte:   s = std::to_string(std::get<i8>(t.v)) + "b"; break;
        case TagType::Short:  s = std::to_string(std::get<i16>(t.v)) + "s"; break;
        case TagType::Int:    s = std::to_string(std::get<i32>(t.v)); break;
        case TagType::Long:   s = std::to_string(std::get<i64>(t.v)) + "L"; break;
        case TagType::Float:  s = std::to_string(std::get<f32>(t.v)) + "f"; break;
        case TagType::Double: s = std::to_string(std::get<f64>(t.v)) + "d"; break;
        case TagType::String: s = '"' + std::get<std::string>(t.v) + '"'; break;
        case TagType::ByteArray:
            s = "[B;" + std::to_string(std::get<std::vector<i8>>(t.v).size()) + " bytes]";
            break;
        case TagType::IntArray:
            s = "[I;" + std::to_string(std::get<std::vector<i32>>(t.v).size()) + " ints]";
            break;
        case TagType::LongArray:
            s = "[L;" + std::to_string(std::get<std::vector<i64>>(t.v).size()) + " longs]";
            break;
        case TagType::List: {
            const auto& l = std::get<List>(t.v);
            s = "[\n";
            for (auto& it : l.items)
                s += pad + "  " + to_snbt(it, indent + 1) + ",\n";
            s += pad + "]";
            break;
        }
        case TagType::Compound: {
            const auto& c = std::get<Compound>(t.v);
            s = "{\n";
            for (auto& [k, v] : c)
                s += pad + "  " + k + ": " + to_snbt(v, indent + 1) + ",\n";
            s += pad + "}";
            break;
        }
        default: s = "END";
    }
    return s;
}

}
