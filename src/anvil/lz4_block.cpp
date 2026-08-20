#include "lz4_block.hpp"
#include <cstring>
#include <stdexcept>

namespace vw::anvil {
namespace {

constexpr u32 kXxhSeed = 0x9747b28cU;
constexpr u32 kP1 = 0x9e3779b1U;
constexpr u32 kP2 = 0x85ebca77U;
constexpr u32 kP3 = 0xc2b2ae3dU;
constexpr u32 kP4 = 0x27d4eb2fU;
constexpr u32 kP5 = 0x165667b1U;

u32 rotl(u32 v, int bits) { return (v << bits) | (v >> (32 - bits)); }
u32 le32(const u8* p) {
    return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
}
u32 round(u32 acc, u32 input) {
    acc += input * kP2;
    acc = rotl(acc, 13);
    return acc * kP1;
}
u32 xxh32(std::span<const u8> input) {
    const u8* p = input.data();
    const u8* const end = p + input.size();
    u32 h;
    if (input.size() >= 16) {
        u32 v1 = kXxhSeed + kP1 + kP2;
        u32 v2 = kXxhSeed + kP2;
        u32 v3 = kXxhSeed;
        u32 v4 = kXxhSeed - kP1;
        const u8* const limit = end - 16;
        do {
            v1 = round(v1, le32(p)); p += 4;
            v2 = round(v2, le32(p)); p += 4;
            v3 = round(v3, le32(p)); p += 4;
            v4 = round(v4, le32(p)); p += 4;
        } while (p <= limit);
        h = rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18);
    } else {
        h = kXxhSeed + kP5;
    }
    h += static_cast<u32>(input.size());
    while (p + 4 <= end) {
        h += le32(p) * kP3;
        h = rotl(h, 17) * kP4;
        p += 4;
    }
    while (p < end) {
        h += u32(*p++) * kP5;
        h = rotl(h, 11) * kP1;
    }
    h ^= h >> 15; h *= kP2;
    h ^= h >> 13; h *= kP3;
    return h ^ (h >> 16);
}

void decode_lz4_payload(std::span<const u8> src, std::span<u8> dst) {
    size_t ip = 0, op = 0;
    while (ip < src.size()) {
        const u8 token = src[ip++];
        size_t literals = token >> 4;
        if (literals == 15) {
            u8 n;
            do {
                if (ip == src.size()) throw std::runtime_error("LZ4: truncated literal length");
                n = src[ip++]; literals += n;
            } while (n == 255);
        }
        if (literals > src.size() - ip || literals > dst.size() - op)
            throw std::runtime_error("LZ4: literal range is invalid");
        std::memcpy(dst.data() + op, src.data() + ip, literals);
        ip += literals; op += literals;
        if (ip == src.size()) break;
        if (ip + 2 > src.size()) throw std::runtime_error("LZ4: truncated match offset");
        const size_t offset = size_t(src[ip]) | (size_t(src[ip + 1]) << 8);
        ip += 2;
        if (offset == 0 || offset > op) throw std::runtime_error("LZ4: invalid match offset");
        size_t match = token & 15;
        if (match == 15) {
            u8 n;
            do {
                if (ip == src.size()) throw std::runtime_error("LZ4: truncated match length");
                n = src[ip++]; match += n;
            } while (n == 255);
        }
        match += 4;
        if (match > dst.size() - op) throw std::runtime_error("LZ4: match exceeds output");
        for (size_t i = 0; i < match; ++i) dst[op + i] = dst[op - offset + i];
        op += match;
    }
    if (op != dst.size()) throw std::runtime_error("LZ4: decompressed size mismatch");
}

}

std::vector<u8> decode_lz4_block_stream(std::span<const u8> source) {
    constexpr std::string_view magic = "LZ4Block";
    constexpr size_t header_size = 21;
    // Жёсткий лимит суммарного результата: NBT одного чанка занимает
    // в разы меньше, а crafted-заголовок с decompressed_size = 0xffffffff
    // не должен заставлять нас выделять гигабайты.
    constexpr size_t max_total_decompressed = 64ull * 1024 * 1024;
    std::vector<u8> out;
    size_t pos = 0;
    while (pos < source.size()) {
        if (source.size() - pos < header_size ||
            std::memcmp(source.data() + pos, magic.data(), magic.size()) != 0)
            throw std::runtime_error("LZ4Block: invalid or truncated header");
        const u8 token = source[pos + 8];
        const u32 compressed_size = le32(source.data() + pos + 9);
        const u32 decompressed_size = le32(source.data() + pos + 13);
        const u32 checksum = le32(source.data() + pos + 17);
        pos += header_size;
        if (compressed_size == 0 && decompressed_size == 0) {
            if (token != 0 || checksum != 0 || pos != source.size())
                throw std::runtime_error("LZ4Block: invalid end marker");
            return out;
        }
        if ((token & 0xf0) != 0x10 && (token & 0xf0) != 0x20)
            throw std::runtime_error("LZ4Block: unsupported compression method");
        if (compressed_size == 0 || decompressed_size == 0 || compressed_size > source.size() - pos)
            throw std::runtime_error("LZ4Block: invalid block size");
        const std::span<const u8> block(source.data() + pos, compressed_size);
        pos += compressed_size;
        const size_t old_size = out.size();
        if (decompressed_size > max_total_decompressed - old_size)
            throw std::runtime_error("LZ4Block: decompressed data exceeds the safety limit");
        out.resize(old_size + decompressed_size);
        if ((token & 0xf0) == 0x10) {
            if (compressed_size != decompressed_size) throw std::runtime_error("LZ4Block: raw size mismatch");
            std::memcpy(out.data() + old_size, block.data(), block.size());
        } else {
            decode_lz4_payload(block, std::span<u8>(out.data() + old_size, decompressed_size));
        }
        if ((xxh32(std::span<const u8>(out.data() + old_size, decompressed_size)) & 0x0fffffffU) != checksum)
            throw std::runtime_error("LZ4Block: checksum mismatch");
    }
    throw std::runtime_error("LZ4Block: missing end marker");
}

}
