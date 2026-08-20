#include "mc_export.hpp"
#include <zlib.h>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <limits>
#include <cstddef>
#include <sstream>
#include <unordered_map>

namespace vw::exporter {

namespace {

// PNG writer (RGBA8).
void put32(std::vector<u8>& v, u32 x) {
    v.push_back(u8(x >> 24)); v.push_back(u8(x >> 16)); v.push_back(u8(x >> 8)); v.push_back(u8(x));
}
u32 crc_of(const u8* p, size_t n) { return static_cast<u32>(crc32(0, p, static_cast<uInt>(n))); }
void png_chunk(std::vector<u8>& out, const char type[4], const std::vector<u8>& data) {
    put32(out, static_cast<u32>(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put32(out, crc_of(&out[start], out.size() - start));
}

enum class AlphaKind { Opaque = 0, Cutout = 1, Translucent = 2 };

AlphaKind tile_alpha_kind(const mesh::TextureAtlas& at, int tile) {
    const auto& reg = at.region(tile);
    const int W = at.width(), H = at.height();
    const int x0 = static_cast<int>(reg.u0 * W), x1 = static_cast<int>(reg.u1 * W);
    const int y0 = static_cast<int>(reg.v0 * H), y1 = static_cast<int>(reg.v1 * H);
    int n = 0, lo = 0, hi = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const u8 a = at.pixels()[(static_cast<size_t>(y) * W + x) * 4 + 3];
            ++n;
            if (a < 8) ++lo;
            else if (a > 247) ++hi;
        }
    if (lo == 0) return AlphaKind::Opaque;

    if (lo + hi >= n * 98 / 100) return AlphaKind::Cutout;
    return AlphaKind::Translucent;
}

std::string sanitize_mtl_name(const std::string& raw) {
    std::string out = "m_";
    for (char c : raw) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9');
        out += ok ? c : '_';
    }
    return out;
}

// Канонические имена материалов для MCprep.
std::string mcprep_material_name(std::string raw) {
    const auto colon = raw.find(':');
    const bool vanilla = colon == std::string::npos || raw.substr(0, colon) == "minecraft";
    if (colon != std::string::npos) raw.erase(0, colon + 1);
    if (raw.starts_with("block/")) raw.erase(0, 6);
    if (raw.ends_with(".png")) raw.resize(raw.size() - 4);
    if (raw == "water_still") raw = "water";
    if (raw == "water_flow") raw = "water_flowing";
    if (raw == "lava_still") raw = "lava";
    if (raw == "lava_flow") raw = "lava_flowing";
    std::string out;
    if (!vanilla) out = "mod_";
    for (char c : raw) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9');
        out += ok ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
    }
    return out.empty() ? "minecraft_material" : out;
}

std::vector<u8> crop_tile_png(const MCBaked& b, const mesh::TextureAtlas& at, int tile) {
    const auto& r = at.region(tile);
    const int x0 = static_cast<int>(r.u0 * b.atlas_w), x1 = static_cast<int>(r.u1 * b.atlas_w);
    const int y0 = static_cast<int>(r.v0 * b.atlas_h), y1 = static_cast<int>(r.v1 * b.atlas_h);
    const int w = x1 - x0, h = y1 - y0;
    std::vector<u8> rgba(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        std::memcpy(rgba.data() + static_cast<size_t>(y) * w * 4,
                    b.atlas_pixels.data() + (static_cast<size_t>(y0 + y) * b.atlas_w + x0) * 4,
                    static_cast<size_t>(w) * 4);
    return encode_png(rgba.data(), w, h);
}

}

std::vector<u8> encode_png(const u8* rgba, int w, int h) {
    std::vector<u8> raw;
    raw.reserve(static_cast<size_t>(h) * (1 + static_cast<size_t>(w) * 4));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + static_cast<size_t>(y) * w * 4,
                   rgba + static_cast<size_t>(y + 1) * w * 4);
    }
    uLongf comp_len = compressBound(static_cast<uLong>(raw.size()));
    std::vector<u8> comp(comp_len);
    compress2(comp.data(), &comp_len, raw.data(), static_cast<uLong>(raw.size()), 6);
    comp.resize(comp_len);

    std::vector<u8> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<u8> ihdr;
    put32(ihdr, static_cast<u32>(w));
    put32(ihdr, static_cast<u32>(h));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    png_chunk(out, "IHDR", ihdr);
    png_chunk(out, "IDAT", comp);
    png_chunk(out, "IEND", {});
    return out;
}

bool bake_mc_export(const MCExportContext& ctx, MCBaked& out) {
    const auto& m = *ctx.mesh;
    if (m.vertices.empty()) return false;
    const auto& at = *ctx.atlas;
    out.mesh = ctx.mesh;
    out.scale = ctx.scale;

    // Копия атласа с запечённым биом-тоном.
    out.atlas_w = at.width();
    out.atlas_h = at.height();
    out.atlas_pixels.assign(at.pixels().begin(), at.pixels().end());
    if (ctx.renders) {
        std::map<int, std::array<f32, 3>> tile_tint;
        for (const auto& br : *ctx.renders) {
            const bool tinted = br.tint[0] != 1 || br.tint[1] != 1 || br.tint[2] != 1;
            if (!tinted) continue;
            const std::array<f32, 3> t{br.tint[0], br.tint[1], br.tint[2]};
            for (const auto& b : br.boxes)
                for (int f = 0; f < 6; ++f)
                    if (b.faces[f].tile >= 0 && b.faces[f].tint)
                        tile_tint[b.faces[f].tile] = t;
            for (const auto& q : br.quads)
                if (q.tile >= 0) tile_tint[q.tile] = t;
            if (br.cross && br.cross_tile >= 0) tile_tint[br.cross_tile] = t;
        }
        const int W = at.width(), H = at.height();
        for (auto& [tile, t] : tile_tint) {
            const auto& reg = at.region(tile);
            const int x0 = static_cast<int>(reg.u0 * W), x1 = static_cast<int>(reg.u1 * W);
            const int y0 = static_cast<int>(reg.v0 * H), y1 = static_cast<int>(reg.v1 * H);
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x) {
                    u8* p = &out.atlas_pixels[(static_cast<size_t>(y) * W + x) * 4];
                    if (p[3] == 0) continue;
                    p[0] = static_cast<u8>(std::clamp(p[0] * t[0], 0.0f, 255.0f));
                    p[1] = static_cast<u8>(std::clamp(p[1] * t[1], 0.0f, 255.0f));
                    p[2] = static_cast<u8>(std::clamp(p[2] * t[2], 0.0f, 255.0f));
                }
        }
    }

    const size_t ntiles = at.tile_count();
    if (ntiles == 0) return false;
    int grid_size = 1;
    while (static_cast<size_t>(grid_size) * grid_size < ntiles * 4) grid_size <<= 1;
    std::vector<int> grid(static_cast<size_t>(grid_size) * grid_size);
    for (int gy = 0; gy < grid_size; ++gy)
        for (int gx = 0; gx < grid_size; ++gx) {
            const f32 u = (gx + 0.5f) / grid_size, v = (gy + 0.5f) / grid_size;
            int best = 0;
            f32 best_d = 1e30f;
            for (size_t i = 0; i < ntiles; ++i) {
                const auto& r = at.region(static_cast<int>(i));
                if (u >= r.u0 && u < r.u1 && v >= r.v0 && v < r.v1) { best = static_cast<int>(i); break; }
                const f32 du = u - (r.u0 + r.u1) * 0.5f, dv = v - (r.v0 + r.v1) * 0.5f;
                const f32 d = du * du + dv * dv;
                if (d < best_d) { best_d = d; best = static_cast<int>(i); }
            }
            grid[static_cast<size_t>(gy) * grid_size + gx] = best;
        }
    auto tile_at_uv = [&](f32 u, f32 v) -> int {
        const int gx = std::clamp(static_cast<int>(u * grid_size), 0, grid_size - 1);
        const int gy = std::clamp(static_cast<int>(v * grid_size), 0, grid_size - 1);
        return grid[static_cast<size_t>(gy) * grid_size + gx];
    };

    std::map<int, std::vector<u32>> faces_by_tile;
    std::map<int, bool> tile_uses_blend;
    bool valid_indices = true;
    auto bucket = [&](const std::vector<u32>& idx, bool blend_pass) {
        if (idx.size() % 6 != 0) { valid_indices = false; return; }
        for (size_t i = 0; i + 5 < idx.size(); i += 6) {
            const u32 q[4] = {idx[i], idx[i + 1], idx[i + 2], idx[i + 5]};
            for (u32 id : q)
                if (id >= m.vertices.size()) { valid_indices = false; return; }
            f32 cu = 0, cv = 0;
            for (u32 id : q) { cu += m.vertices[id].u; cv += m.vertices[id].v; }
            const int tile = tile_at_uv(cu / 4.0f, cv / 4.0f);
            if (blend_pass) tile_uses_blend[tile] = true;
            auto& vec = faces_by_tile[tile];
            vec.insert(vec.end(), idx.begin() + static_cast<ptrdiff_t>(i),
                       idx.begin() + static_cast<ptrdiff_t>(i + 6));
        }
    };
    bucket(m.indices, false);
    bucket(m.indices_alpha, true);
    if (!valid_indices || faces_by_tile.empty()) return false;

    std::unordered_map<std::string, int> name_owner;
    for (auto& [tile, _] : faces_by_tile) {
        std::string n = sanitize_mtl_name(at.tile_name(tile));
        if (auto it = name_owner.find(n); it != name_owner.end() && it->second != tile)
            n += "_" + std::to_string(tile);
        name_owner[n] = tile;
        out.mtl_name[tile] = std::move(n);
        out.tile_alpha[tile] = tile_uses_blend[tile]
            ? static_cast<int>(AlphaKind::Translucent)
            : static_cast<int>(tile_alpha_kind(at, tile));
    }

    // Сварка вершин и отбраковка точных дубликатов квадов.
    auto qf = [](f32 x) -> i32 { return static_cast<i32>(std::lround(x * 10000.0f)); };
    std::map<std::array<i32, 11>, u32> weld_map;
    std::vector<u32> old_to_new(m.vertices.size(), std::numeric_limits<u32>::max());
    auto weld = [&](u32 old) -> u32 {
        u32& cached = old_to_new[old];
        if (cached != std::numeric_limits<u32>::max()) return cached;
        const auto& v = m.vertices[old];
        const std::array<i32, 11> k{qf(v.px), qf(v.py), qf(v.pz), qf(v.u), qf(v.v),
                                    qf(v.nx), qf(v.ny), qf(v.nz), qf(v.r), qf(v.g), qf(v.b)};
        auto [it, ins] = weld_map.try_emplace(k, static_cast<u32>(out.vert_src.size()));
        if (ins) out.vert_src.push_back(old);
        cached = it->second;
        return cached;
    };

    std::set<std::array<u32, 5>> seen;
    for (auto& [tile, vec] : faces_by_tile) {
        for (size_t i = 0; i + 5 < vec.size(); i += 6) {
            const bool quad_ok = vec[i + 3] == vec[i] && vec[i + 4] == vec[i + 2];
            if (!quad_ok) continue;
            const u32 old4[4] = {vec[i], vec[i + 1], vec[i + 2], vec[i + 5]};
            MCBaked::Quad q{tile, {weld(old4[0]), weld(old4[1]), weld(old4[2]), weld(old4[3])}};
            std::array<u32, 5> key{q.w[0], q.w[1], q.w[2], q.w[3], static_cast<u32>(tile)};
            std::sort(key.begin(), key.begin() + 4);
            if (!seen.insert(key).second) continue;
            // Обход наружу.
            const auto& v0 = m.vertices[out.vert_src[q.w[0]]];
            const auto& a1 = m.vertices[out.vert_src[q.w[1]]];
            const auto& a2 = m.vertices[out.vert_src[q.w[2]]];
            const f32 U1 = a1.px - v0.px, U2 = a1.py - v0.py, U3 = a1.pz - v0.pz;
            const f32 V1 = a2.px - v0.px, V2 = a2.py - v0.py, V3 = a2.pz - v0.pz;
            const f32 dot = (U2 * V3 - U3 * V2) * v0.nx +
                            (U3 * V1 - U1 * V3) * v0.ny +
                            (U1 * V2 - U2 * V1) * v0.nz;
            if (dot < 0) std::swap(q.w[1], q.w[3]);
            out.quads.push_back(q);
        }
    }
    return true;
}

bool export_mc_obj(const MCExportContext& ctx, const std::filesystem::path& obj_path) {
    MCBaked b;
    if (!bake_mc_export(ctx, b)) return false;
    const auto& m = *b.mesh;
    const auto stem = obj_path.stem().string();
    const auto dir = obj_path.parent_path();

    // По одному PNG на тайл; модовые материалы — с префиксом mod_.
    std::map<int, std::string> obj_name;
    std::set<std::string> used_names;
    for (const auto& [tile, _] : b.mtl_name) {
        std::string n = mcprep_material_name(ctx.atlas->tile_name(tile));
        const std::string base = n;
        for (int suffix = 2; !used_names.insert(n).second; ++suffix)
            n = base + "_" + std::to_string(suffix);
        obj_name[tile] = std::move(n);
    }
    for (const auto& [tile, name] : obj_name) {
        const auto png = crop_tile_png(b, *ctx.atlas, tile);
        std::ofstream f(dir / (stem + "_" + name + ".png"), std::ios::binary);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        if (!f) return false;
    }

    // .mtl
    {
        std::ofstream f(dir / (stem + ".mtl"));
        if (!f) return false;
        f << "# Dine 1.0.0 MTL\n";
        for (const auto& [tile, name] : obj_name) {
            const auto ak = static_cast<AlphaKind>(b.tile_alpha[tile]);
            const bool translucent = ak == AlphaKind::Translucent;
            const f32 alpha = translucent ? 0.85f : 1.0f;
            f << "newmtl " << name << "\n"
              << "Ns 0\n"
              << "Ka 1 1 1\n"
              << "Kd 1 1 1\n"
              << "Ks 0 0 0\n"
              << "illum " << (translucent ? 4 : 2) << "\n"
              << "d " << alpha << "\n"
              << "Tr " << (1.0f - alpha) << "\n"
              << "map_Kd " << stem << '_' << name << ".png\n";
            if (ak != AlphaKind::Opaque)
                f << "map_d " << stem << '_' << name << ".png\n";
        }
    }

    std::ofstream f(obj_path);
    if (!f) return false;

    // Имя объекта с габаритами.
    const f32 s = b.scale;
    f32 mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (u32 old : b.vert_src) {
        const auto& v = m.vertices[old];
        const f32 p[3] = {v.px * s, v.py * s, v.pz * s};
        for (int i = 0; i < 3; ++i) {
            mn[i] = std::min(mn[i], p[i]);
            mx[i] = std::max(mx[i], p[i]);
        }
    }

    f << "# Wavefront OBJ file made by Dine 1.0.0\n"
      << "mtllib " << stem << ".mtl\n"
      << "o " << stem << "__" << static_cast<i64>(std::floor(mn[0])) << '_'
      << static_cast<i64>(std::floor(mn[1])) << '_'
      << static_cast<i64>(std::floor(mn[2])) << "_to_"
      << static_cast<i64>(std::ceil(mx[0])) << '_'
      << static_cast<i64>(std::ceil(mx[1])) << '_'
      << static_cast<i64>(std::ceil(mx[2])) << "\n";

    for (u32 old : b.vert_src) {
        const auto& v = m.vertices[old];
        f << "vn " << v.nx << ' ' << v.ny << ' ' << v.nz << '\n';
    }
    // UV: из координат атласа в [0,1] тайла.
    std::vector<int> vertex_tile(b.vert_src.size(), -1);
    for (const auto& q : b.quads)
        for (u32 w : q.w)
            if (vertex_tile[w] < 0) vertex_tile[w] = q.tile;
    for (size_t i = 0; i < b.vert_src.size(); ++i) {
        const auto& v = m.vertices[b.vert_src[i]];
        const int tile = vertex_tile[i] < 0 ? 0 : vertex_tile[i];
        const auto& r = ctx.atlas->region(tile);
        const f32 u = (v.u - r.u0) / (r.u1 - r.u0);
        const f32 vv = (v.v - r.v0) / (r.v1 - r.v0);
        f << "vt " << u << ' ' << 1.0f - vv << '\n';
    }
    for (u32 old : b.vert_src) {
        const auto& v = m.vertices[old];
        f << "v " << v.px * s << ' ' << v.py * s << ' ' << v.pz * s << '\n';
    }

    // f v/vt/vn; вырожденный квад -> треугольник.
    auto face = [&](u32 a, u32 b2, u32 c, u32 d) {
        if (c == d)
            f << "f " << a + 1 << '/' << a + 1 << '/' << a + 1 << ' '
              << b2 + 1 << '/' << b2 + 1 << '/' << b2 + 1 << ' '
              << c + 1 << '/' << c + 1 << '/' << c + 1 << '\n';
        else
            f << "f " << a + 1 << '/' << a + 1 << '/' << a + 1 << ' '
              << b2 + 1 << '/' << b2 + 1 << '/' << b2 + 1 << ' '
              << c + 1 << '/' << c + 1 << '/' << c + 1 << ' '
              << d + 1 << '/' << d + 1 << '/' << d + 1 << '\n';
    };
    int cur_tile = -1;
    for (const auto& q : b.quads) {
        if (q.tile != cur_tile) {
            cur_tile = q.tile;
            // Группа по типу блока.
            f << "g " << obj_name[cur_tile] << '\n';
            f << "usemtl " << obj_name[cur_tile] << '\n';
        }
        face(q.w[0], q.w[1], q.w[2], q.w[3]);
    }
    return true;
}


namespace {

void align4(std::vector<u8>& v, u8 pad = 0) { while (v.size() % 4) v.push_back(pad); }

}

bool export_mc_glb(const MCExportContext& ctx, const std::filesystem::path& glb_path) {
    const auto& m = *ctx.mesh;
    if (m.vertices.empty()) return false;
    const auto& at = *ctx.atlas;

    // GLB: COLOR_0 несёт биом-тон; атлас без запечки тона.
    MCBaked b;
    {
        MCExportContext no_tint{ctx.mesh, ctx.atlas, ctx.scale, nullptr};
        if (!bake_mc_export(no_tint, b)) return false;
    }
    const size_t nv = b.vert_src.size();
    for (const auto& [tile, kind] : b.tile_alpha) {
        if (static_cast<AlphaKind>(kind) != AlphaKind::Translucent) continue;
        const auto& r = at.region(tile);
        const int x0 = static_cast<int>(r.u0 * b.atlas_w), x1 = static_cast<int>(r.u1 * b.atlas_w);
        const int y0 = static_cast<int>(r.v0 * b.atlas_h), y1 = static_cast<int>(r.v1 * b.atlas_h);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                u8& a = b.atlas_pixels[(static_cast<size_t>(y) * b.atlas_w + x) * 4 + 3];
                a = a < 5 ? 0 : static_cast<u8>(std::clamp(static_cast<int>(a), 102, 217));
            }
    }
    auto png = encode_png(b.atlas_pixels.data(), b.atlas_w, b.atlas_h);

    // Один меш с максимум тремя примитивами.
    struct Prim { AlphaKind alpha; std::vector<const MCBaked::Quad*> quads; };
    std::vector<Prim> prims;
    for (int kind = 0; kind < 3; ++kind) {
        Prim p{static_cast<AlphaKind>(kind), {}};
        for (const auto& q : b.quads)
            if (static_cast<AlphaKind>(b.tile_alpha[q.tile]) == p.alpha)
                p.quads.push_back(&q);
        if (!p.quads.empty()) prims.push_back(std::move(p));
    }

    std::vector<u8> bin;
    auto push_f32 = [&](f32 x) {
        const size_t o = bin.size();
        bin.resize(o + 4);
        std::memcpy(&bin[o], &x, 4);
    };

    const f32 s = b.scale;
    f32 mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    const size_t off_pos = bin.size();
    for (u32 old : b.vert_src) {
        const auto& v = m.vertices[old];
        const f32 p[3] = {v.px * s, v.py * s, v.pz * s};
        for (int i = 0; i < 3; ++i) {
            mn[i] = std::min(mn[i], p[i]);
            mx[i] = std::max(mx[i], p[i]);
            push_f32(p[i]);
        }
    }
    const size_t off_nrm = bin.size();
    for (u32 old : b.vert_src) {
        const auto& v = m.vertices[old];
        push_f32(v.nx); push_f32(v.ny); push_f32(v.nz);
    }
    const size_t off_uv = bin.size();
    for (u32 old : b.vert_src) {
        const auto& v = m.vertices[old];
        push_f32(v.u); push_f32(v.v);
    }
    const size_t off_col = bin.size();
    for (u32 old : b.vert_src) {
        const auto& v = m.vertices[old];
        push_f32(v.r); push_f32(v.g); push_f32(v.b);
    }

    // индексы по примитивам
    std::vector<size_t> off_idx, cnt_idx;
    for (const auto& p : prims) {
        align4(bin);
        off_idx.push_back(bin.size());
        const size_t base = bin.size();
        bin.resize(base + p.quads.size() * 6 * 4);
        u32* dst = reinterpret_cast<u32*>(bin.data() + base);
        size_t n = 0;
        for (const auto* q : p.quads) {
            dst[n++] = q->w[0]; dst[n++] = q->w[1]; dst[n++] = q->w[2];
            dst[n++] = q->w[0]; dst[n++] = q->w[2]; dst[n++] = q->w[3];
        }
        cnt_idx.push_back(n);
    }
    align4(bin);
    const size_t off_png = bin.size();
    bin.insert(bin.end(), png.begin(), png.end());
    align4(bin);

    std::ostringstream j;
    j << R"({"asset":{"version":"2.0","generator":"Dine 1.0.0"},)"
      << R"("scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0,"name":"Dine_World"}],)";

    // Один объект с максимум тремя слоями материалов.
    j << R"("meshes":[{"name":"Dine_World","primitives":[)";
    for (size_t i = 0; i < prims.size(); ++i) {
        if (i) j << ',';
        j << R"({"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2,"COLOR_0":3},)"
          << R"("indices":)" << (4 + i) << R"(,"material":)" << static_cast<int>(prims[i].alpha) << '}';
    }
    j << "]}],";

    j << R"("materials":[)";
    for (int kind = 0; kind < 3; ++kind) {
        if (kind) j << ',';
        const auto ak = static_cast<AlphaKind>(kind);
        const char* material_name = ak == AlphaKind::Opaque ? "dine_opaque"
                                  : ak == AlphaKind::Cutout ? "dine_cutout" : "dine_translucent";
        j << R"({"name":")" << material_name << R"(","pbrMetallicRoughness":{"baseColorTexture":{"index":0},"metallicFactor":0,"roughnessFactor":1})";
        if (ak == AlphaKind::Cutout) j << R"(,"alphaMode":"MASK","alphaCutoff":0.5,"doubleSided":true)";
        else if (ak == AlphaKind::Translucent) j << R"(,"alphaMode":"BLEND","doubleSided":true)";
        j << '}';
    }
    j << "],";

    j << R"("textures":[{"source":0,"sampler":0}],)"
      << R"("samplers":[{"magFilter":9728,"minFilter":9987,"wrapS":33071,"wrapT":33071}],)"
      << R"("images":[{"bufferView":)" << (4 + prims.size()) << R"(,"mimeType":"image/png"}],)";

    j << R"("accessors":[)"
      << R"({"bufferView":0,"componentType":5126,"count":)" << nv << R"(,"type":"VEC3",)"
      << R"("min":[)" << mn[0] << ',' << mn[1] << ',' << mn[2] << R"(],"max":[)"
      << mx[0] << ',' << mx[1] << ',' << mx[2] << "]},"
      << R"({"bufferView":1,"componentType":5126,"count":)" << nv << R"(,"type":"VEC3"},)"
      << R"({"bufferView":2,"componentType":5126,"count":)" << nv << R"(,"type":"VEC2"},)"
      << R"({"bufferView":3,"componentType":5126,"count":)" << nv << R"(,"type":"VEC3"})";
    for (size_t i = 0; i < prims.size(); ++i)
        j << R"(,{"bufferView":)" << (4 + i) << R"(,"componentType":5125,"count":)"
          << cnt_idx[i] << R"(,"type":"SCALAR"})";
    j << "],";

    j << R"("bufferViews":[)"
      << R"({"buffer":0,"byteOffset":)" << off_pos << R"(,"byteLength":)" << nv * 12 << R"(,"target":34962},)"
      << R"({"buffer":0,"byteOffset":)" << off_nrm << R"(,"byteLength":)" << nv * 12 << R"(,"target":34962},)"
      << R"({"buffer":0,"byteOffset":)" << off_uv << R"(,"byteLength":)" << nv * 8 << R"(,"target":34962},)"
      << R"({"buffer":0,"byteOffset":)" << off_col << R"(,"byteLength":)" << nv * 12 << R"(,"target":34962})";
    for (size_t i = 0; i < prims.size(); ++i)
        j << R"(,{"buffer":0,"byteOffset":)" << off_idx[i] << R"(,"byteLength":)"
          << cnt_idx[i] * 4 << R"(,"target":34963})";
    j << R"(,{"buffer":0,"byteOffset":)" << off_png << R"(,"byteLength":)" << png.size() << "}],";

    j << R"("buffers":[{"byteLength":)" << bin.size() << "}]}";

    std::string json = j.str();
    while (json.size() % 4) json += ' ';

    std::ofstream f(glb_path, std::ios::binary);
    if (!f) return false;
    auto w32 = [&](u32 x) { f.write(reinterpret_cast<const char*>(&x), 4); };
    w32(0x46546C67);
    w32(2);
    w32(static_cast<u32>(12 + 8 + json.size() + 8 + bin.size()));
    w32(static_cast<u32>(json.size()));
    w32(0x4E4F534A);
    f.write(json.data(), static_cast<std::streamsize>(json.size()));
    w32(static_cast<u32>(bin.size()));
    w32(0x004E4942);
    f.write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));
    return true;
}

size_t export_mc_glb_chunks(const MCExportContext& ctx, const std::filesystem::path& directory,
                            const std::string& base_name, i32 chunk_blocks) {
    if (!ctx.mesh || !ctx.atlas || chunk_blocks < 4) return 0;
    const auto& src = *ctx.mesh;
    if (src.vertices.empty()) return 0;
    std::filesystem::create_directories(directory);

    struct Bucket { render::MCMesh mesh; std::unordered_map<u32, u32> remap; };
    std::map<std::pair<i32, i32>, Bucket> buckets;
    auto append = [&](const std::vector<u32>& indices, bool alpha) {
        for (size_t i = 0; i + 5 < indices.size(); i += 6) {
            const u32 a = indices[i], b = indices[i + 1], c = indices[i + 2], d = indices[i + 5];
            if (a >= src.vertices.size() || b >= src.vertices.size() ||
                c >= src.vertices.size() || d >= src.vertices.size()) continue;
            const auto& p = src.vertices[a];
            const i32 bx = static_cast<i32>(std::floor(p.px / chunk_blocks));
            const i32 bz = static_cast<i32>(std::floor(p.pz / chunk_blocks));
            Bucket& out = buckets[{bx, bz}];
            auto map_vertex = [&](u32 old) {
                auto [it, inserted] = out.remap.try_emplace(old, static_cast<u32>(out.mesh.vertices.size()));
                if (inserted) out.mesh.vertices.push_back(src.vertices[old]);
                return it->second;
            };
            auto& dst = alpha ? out.mesh.indices_alpha : out.mesh.indices;
            for (size_t k = 0; k < 6; ++k) dst.push_back(map_vertex(indices[i + k]));
        }
    };
    append(src.indices, false);
    append(src.indices_alpha, true);

    size_t written = 0;
    for (auto& [cell, bucket] : buckets) {
        MCExportContext part{&bucket.mesh, ctx.atlas, ctx.scale, ctx.renders};
        const auto name = base_name + "_x" + std::to_string(cell.first) + "_z" +
                          std::to_string(cell.second) + ".glb";
        if (export_mc_glb(part, directory / name)) ++written;
    }
    return written;
}

}
