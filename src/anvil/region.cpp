#include "region.hpp"
#include "lz4_block.hpp"
#include <fstream>
#include <charconv>

namespace vw::anvil {

namespace {
u32 be32(const u8* p) {
    return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
}
}

RegionFile::RegionFile(const std::filesystem::path& path) {

    // Строго r.<int>.<int>.mca — иначе координаты региона не определить,
    // а npos в арифметике указателей был бы undefined behavior.
    // Учитываем отрицательные координаты (r.-1.-1.mca): в stem остаётся одна точка.
    const auto stem = path.stem().string();
    const auto p1 = stem.size() > 2 && stem[0] == 'r' ? stem.find('.', 2) : std::string::npos;
    if (p1 == std::string::npos)
        throw std::runtime_error("bad region file name: " + path.filename().string());
    {
        const char* const x0 = stem.data() + 2;
        const char* const x1 = stem.data() + p1;
        const char* const z0 = stem.data() + p1 + 1;
        const char* const z1 = stem.data() + stem.size();
        const auto rx = std::from_chars(x0, x1, rx_);
        const auto rz = std::from_chars(z0, z1, rz_);
        if (rx.ec != std::errc{} || rx.ptr != x1 || rz.ec != std::errc{} || rz.ptr != z1)
            throw std::runtime_error("bad region file name: " + path.filename().string());
    }
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) throw std::runtime_error("cannot open region: " + path.string());
    auto sz = static_cast<size_t>(f.tellg());
    if (sz < 8192) throw std::runtime_error("region file too small: " + path.string());
    data_.resize(sz);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(data_.data()), static_cast<std::streamsize>(sz));
}

bool RegionFile::has_chunk(int lx, int lz) const {
    const size_t idx = static_cast<size_t>((lz & 31) * 32 + (lx & 31)) * 4;
    return be32(&data_[idx]) != 0;
}

std::optional<nbt::Document> RegionFile::read_chunk(int lx, int lz) const {
    const size_t idx = static_cast<size_t>((lz & 31) * 32 + (lx & 31)) * 4;
    const u32 loc = be32(&data_[idx]);
    if (loc == 0) return std::nullopt;

    const u64 offset = static_cast<u64>(loc >> 8) * 4096;
    const u32 sectors = loc & 0xff;
    if (offset + 5 > data_.size() || sectors == 0) return std::nullopt;

    const u8* p = data_.data() + offset;
    const u32 length = be32(p);
    if (length < 1 || offset + 4 + length > data_.size()) return std::nullopt;
    // Чанк обязан помещаться в выделенные ему секторы таблицы размещения,
    // иначе он «залезает» в чужие данные.
    if (static_cast<u64>(length) + 4 > static_cast<u64>(sectors) * 4096)
        return std::nullopt;

    u8 comp = p[4];
    if (comp & 0x80)
        return std::nullopt;

    std::span<const u8> payload(p + 5, length - 1);
    switch (comp) {
        case 1: return nbt::parse(nbt::inflate(payload, true));
        case 2: return nbt::parse(nbt::inflate(payload, true));
        case 3: return nbt::parse(payload);
        case 4: return nbt::parse(decode_lz4_block_stream(payload));
        default: return std::nullopt;
    }
}

}
