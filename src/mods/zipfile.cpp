#include "zipfile.hpp"
#include <zlib.h>
#include <fstream>
#include <cstring>

namespace vw::zip {

namespace {
u16 le16(const u8* p) { return u16(p[0]) | (u16(p[1]) << 8); }
u32 le32(const u8* p) { return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24); }
u64 le64(const u8* p) { u64 v = 0; for (int i = 7; i >= 0; --i) v = (v << 8) | p[i]; return v; }

constexpr u32 SIG_EOCD   = 0x06054b50;
constexpr u32 SIG_EOCD64 = 0x06064b50;
constexpr u32 SIG_LOC64  = 0x07064b50;
constexpr u32 SIG_CDIR   = 0x02014b50;
constexpr u32 SIG_LOCAL  = 0x04034b50;

constexpr size_t MAX_ENTRY_NAME = 4096;
constexpr size_t MAX_EXTRA = 65535;
constexpr u64 MAX_UNCOMP_SIZE = 512ull * 1024 * 1024;
}

ZipFile::ZipFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot open zip: " + path.string());
    const auto sz = f.tellg();
    if (sz < 0) throw std::runtime_error("cannot stat zip: " + path.string());
    data_.resize(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(data_.data()), static_cast<std::streamsize>(data_.size()));
    if (!f) throw std::runtime_error("cannot read zip: " + path.string());
    parse_central_directory();
}

void ZipFile::parse_central_directory() {
    if (data_.size() < 22) throw std::runtime_error("zip too small");

    size_t eocd = SIZE_MAX;
    const size_t scan_from = data_.size() >= 22 + 65535 ? data_.size() - 22 - 65535 : 0;
    for (size_t i = data_.size() - 22 + 1; i-- > scan_from;) {
        if (le32(&data_[i]) == SIG_EOCD) { eocd = i; break; }
        if (i == scan_from) break;
    }
    if (eocd == SIZE_MAX) throw std::runtime_error("zip: EOCD not found");

    if (eocd + 22 > data_.size()) throw std::runtime_error("zip: EOCD truncated");

    u64 cd_count  = le16(&data_[eocd + 10]);
    u64 cd_size   = le32(&data_[eocd + 12]);
    u64 cd_offset = le32(&data_[eocd + 16]);

    if ((cd_count == 0xffff || cd_offset == 0xffffffff || cd_size == 0xffffffff) &&
        eocd >= 20 && eocd - 20 + 20 <= data_.size() && le32(&data_[eocd - 20]) == SIG_LOC64) {
        const u64 eocd64 = le64(&data_[eocd - 20 + 8]);
        if (eocd64 + 56 <= data_.size() && le32(&data_[eocd64]) == SIG_EOCD64) {
            cd_count  = le64(&data_[eocd64 + 32]);
            cd_size   = le64(&data_[eocd64 + 40]);
            cd_offset = le64(&data_[eocd64 + 48]);
        }
    }

    if (cd_offset > data_.size() || cd_size > data_.size() || cd_offset + cd_size > data_.size())
        throw std::runtime_error("zip: central directory out of bounds");
    if (cd_count > 100000) throw std::runtime_error("zip: too many entries");

    size_t p = static_cast<size_t>(cd_offset);
    entries_.reserve(static_cast<size_t>(std::min<u64>(cd_count, 100000)));
    for (u64 i = 0; i < cd_count; ++i) {
        if (p + 46 > data_.size()) break;
        if (le32(&data_[p]) != SIG_CDIR) break;
        const u16 nlen = le16(&data_[p + 28]);
        const u16 xlen = le16(&data_[p + 30]);
        const u16 clen = le16(&data_[p + 32]);
        if (nlen > MAX_ENTRY_NAME) throw std::runtime_error("zip: filename too long");
        if (xlen > MAX_EXTRA) throw std::runtime_error("zip: extra field too large");
        if (p + 46 + nlen > data_.size()) throw std::runtime_error("zip: truncated filename");
        if (p + 46 + nlen + xlen > data_.size()) throw std::runtime_error("zip: truncated extra");
        if (p + 46 + nlen + xlen + clen > data_.size()) throw std::runtime_error("zip: truncated comment");

        Entry e;
        e.method      = le16(&data_[p + 10]);
        e.crc32       = le32(&data_[p + 16]);
        e.comp_size   = le32(&data_[p + 20]);
        e.uncomp_size = le32(&data_[p + 24]);
        e.local_header_offset = le32(&data_[p + 42]);
        e.name.assign(reinterpret_cast<const char*>(&data_[p + 46]), nlen);

        size_t xp = p + 46 + nlen;
        const size_t xend = xp + xlen;
        if (xend > data_.size()) throw std::runtime_error("zip: extra field out of bounds");
        while (xp + 4 <= xend) {
            const u16 id = le16(&data_[xp]);
            const u16 sz = le16(&data_[xp + 2]);
            if (xp + 4 + sz > xend) throw std::runtime_error("zip: extra field truncated");
            if (id == 0x0001) {
                size_t q = xp + 4;
                if (e.uncomp_size == 0xffffffff) {
                    if (q + 8 > xp + 4 + sz) throw std::runtime_error("zip: zip64 uncomp truncated");
                    e.uncomp_size = le64(&data_[q]); q += 8;
                }
                if (e.comp_size == 0xffffffff) {
                    if (q + 8 > xp + 4 + sz) throw std::runtime_error("zip: zip64 comp truncated");
                    e.comp_size = le64(&data_[q]); q += 8;
                }
                if (e.local_header_offset == 0xffffffff) {
                    if (q + 8 > xp + 4 + sz) throw std::runtime_error("zip: zip64 offset truncated");
                    e.local_header_offset = le64(&data_[q]); q += 8;
                }
            }
            xp += 4 + sz;
        }

        if (e.uncomp_size > MAX_UNCOMP_SIZE) throw std::runtime_error("zip: entry too large: " + e.name);

        if (auto it = index_.find(e.name); it != index_.end()) {
            entries_[it->second] = std::move(e);
        } else {
            index_.emplace(e.name, entries_.size());
            entries_.push_back(std::move(e));
        }
        p += 46 + nlen + xlen + clen;
    }
}

const Entry* ZipFile::find(std::string_view name) const {
    auto it = index_.find(std::string(name));
    return it == index_.end() ? nullptr : &entries_[it->second];
}

std::vector<u8> ZipFile::read(const Entry& e) const {
    const size_t lh = static_cast<size_t>(e.local_header_offset);
    if (lh + 30 > data_.size() || le32(&data_[lh]) != SIG_LOCAL)
        throw std::runtime_error("zip: bad local header for " + e.name);
    const u16 nlen = le16(&data_[lh + 26]);
    const u16 xlen = le16(&data_[lh + 28]);
    if (lh + 30 + nlen + xlen > data_.size())
        throw std::runtime_error("zip: local header truncated for " + e.name);
    const size_t off = lh + 30 + nlen + xlen;
    if (e.comp_size > data_.size() || off > data_.size() || off + e.comp_size > data_.size())
        throw std::runtime_error("zip: entry out of bounds: " + e.name);
    if (e.uncomp_size > MAX_UNCOMP_SIZE)
        throw std::runtime_error("zip: uncompressed too large: " + e.name);

    std::span<const u8> comp(&data_[off], static_cast<size_t>(e.comp_size));

    std::vector<u8> out;
    if (e.method == 0) {
        // Stored: данные идут как есть, но размер и CRC всё равно проверяем.
        if (e.comp_size != e.uncomp_size)
            throw std::runtime_error("zip: stored size mismatch for " + e.name);
        out.assign(comp.begin(), comp.end());
    } else {
        if (e.method != 8) throw std::runtime_error("zip: unsupported method for " + e.name);

        out.resize(static_cast<size_t>(e.uncomp_size));
        if (!out.empty()) {
            z_stream zs{};
            if (inflateInit2(&zs, -15) != Z_OK) throw std::runtime_error("zlib init failed");
            zs.next_in   = const_cast<Bytef*>(comp.data());
            zs.avail_in  = static_cast<uInt>(comp.size());
            zs.next_out  = out.data();
            zs.avail_out = static_cast<uInt>(out.size());
            const int rc = ::inflate(&zs, Z_FINISH);
            const uLong produced = zs.total_out;
            inflateEnd(&zs);
            // Только Z_STREAM_END: Z_OK означает обрезанный/неполный поток.
            if (rc != Z_STREAM_END)
                throw std::runtime_error("zip: inflate failed for " + e.name);
            if (produced != e.uncomp_size)
                throw std::runtime_error("zip: uncompressed size mismatch for " + e.name);
        }
    }
    if (!out.empty() && ::crc32(::crc32(0L, Z_NULL, 0), out.data(),
                                static_cast<uInt>(out.size())) != e.crc32)
        throw std::runtime_error("zip: CRC mismatch for " + e.name);
    return out;
}

std::optional<std::vector<u8>> ZipFile::read(std::string_view name) const {
    const Entry* e = find(name);
    if (!e) return std::nullopt;
    return read(*e);
}

}
