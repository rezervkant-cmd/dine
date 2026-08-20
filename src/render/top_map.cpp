#include "top_map.hpp"
#include <algorithm>
#include <mutex>

namespace vw::render {

namespace {

std::optional<std::array<f32, 3>> map_tint_rgb(std::string_view n) {
    auto has = [&](std::string_view s) { return n.find(s) != std::string_view::npos; };
    if (has("grass_block") || n == "minecraft:grass" || n == "minecraft:short_grass" ||
        has("tall_grass") || has("fern") || has("sugar_cane"))
        return std::array<f32, 3>{0.57f, 0.74f, 0.35f};
    if (has("leaves") || has("vine"))
        return std::array<f32, 3>{0.47f, 0.67f, 0.18f};
    if (has("water")) return std::array<f32, 3>{0.25f, 0.46f, 0.89f};
    if (has("lava"))  return std::array<f32, 3>{0.98f, 0.45f, 0.10f};
    return std::nullopt;
}

bool map_transparent(std::string_view n) {
    return n.find("glass") != std::string_view::npos;
}

}

std::array<u8, 4> top_map_color(const BlockState& bs, const mods::AssetRegistry* assets) {
    std::array<u8, 4> c{0, 0, 0, 255};
    bool have = false;
    if (assets) {
        if (auto avg = const_cast<mods::AssetRegistry*>(assets)->average_color(bs.name)) {
            c = *avg;
            have = true;
        }
    }
    if (!have) c = fallback_block_color(bs.name);
    c[3] = 255;
    if (auto t = map_tint_rgb(bs.name)) {
        c[0] = static_cast<u8>(std::clamp(c[0] * (*t)[0] * 1.6f, 0.0f, 255.0f));
        c[1] = static_cast<u8>(std::clamp(c[1] * (*t)[1] * 1.6f, 0.0f, 255.0f));
        c[2] = static_cast<u8>(std::clamp(c[2] * (*t)[2] * 1.6f, 0.0f, 255.0f));
    }
    return c;
}

TopMap build_top_map(anvil::World& world,
                     const mods::AssetRegistry* assets,
                     i32 x0, i32 z0, i32 x1, i32 z1,
                     i32 y_max, ThreadPool* pool) {
    TopMap m;
    if (x1 < x0 || z1 < z0) return m;
    m.x0 = x0; m.z0 = z0;
    m.w = x1 - x0 + 1;
    m.h = z1 - z0 + 1;
    m.y_max = y_max;
    m.rgba.assign(static_cast<size_t>(m.w) * m.h * 4, 0);

    ThreadPool local_pool;
    ThreadPool& work_pool = pool ? *pool : local_pool;
    auto chunks = world.load_area(work_pool, x0 >> 4, z0 >> 4, x1 >> 4, z1 >> 4);

    auto pal_snap = world.palette_snapshot();
    const size_t pal_size = pal_snap.size();
    std::vector<std::array<u8, 4>> colors(pal_size);
    for (size_t i = 0; i < pal_size; ++i) {
        const BlockState& bs = pal_snap[i];
        if (bs.is_air()) { colors[i] = {0, 0, 0, 0}; continue; }
        colors[i] = top_map_color(bs, assets);
        if (map_transparent(bs.name)) colors[i][3] = 64;
    }

    const size_t npx = static_cast<size_t>(m.w) * m.h;
    std::vector<i32> heights(npx, std::numeric_limits<i32>::min());
    std::vector<u16> top_id(npx, 0);

    struct ChunkSpan { const Chunk* ch; i32 cx0, cz0; };
    std::vector<ChunkSpan> spans;
    spans.reserve(chunks.size());
    for (const auto& c : chunks) {
        spans.push_back({&c, c.cx * 16, c.cz * 16});
    }

    const i32 sec_top = y_max >> 4;
    auto scan_column = [&](size_t col_x) {
        const i32 wx = x0 + static_cast<i32>(col_x);
        const i32 cx = wx >> 4;
        for (const auto& sp : spans) {
            if (sp.ch->cx != cx) continue;
            const int lx = wx & 15;
            for (i32 wz_i = 0; wz_i < m.h; ++wz_i) {
                const i32 wz = z0 + wz_i;
                if ((wz >> 4) != sp.ch->cz) continue;
                const int lz = wz & 15;

                bool found = false;
                for (i32 sy = sec_top; sy >= -8 && !found; --sy) {
                    const Section* s = sp.ch->section_at(sy);
                    if (!s) continue;
                    const i32 y_hi = (sy == sec_top) ? std::min(15, y_max - sy * 16) : 15;
                    for (int ly = y_hi; ly >= 0; --ly) {
                        u16 id = s->at(lx, ly, lz);
                        if (id >= pal_snap.size()) continue;
                        if (id == 0 || pal_snap[id].is_air()) continue;
                        const size_t pi = static_cast<size_t>(wz_i) * m.w + col_x;
                        top_id[pi] = id;
                        heights[pi] = sy * 16 + ly;
                        found = true;
                        break;
                    }
                }
            }
        }
    };

    if (pool)
        pool->parallel_for(0, static_cast<size_t>(m.w), [&](size_t x) { scan_column(x); });
    else
        for (size_t x = 0; x < static_cast<size_t>(m.w); ++x) scan_column(x);

    i32 hmin = std::numeric_limits<i32>::max(), hmax = std::numeric_limits<i32>::min();
    for (i32 h : heights) {
        if (h == std::numeric_limits<i32>::min()) continue;
        hmin = std::min(hmin, h);
        hmax = std::max(hmax, h);
    }
    if (hmin > hmax) { hmin = 0; hmax = 0; }
    m.y_min = hmin;
    m.y_peak = hmax;
    const f32 hspan = hmax > hmin ? static_cast<f32>(hmax - hmin) : 1.0f;

    for (size_t i = 0; i < npx; ++i) {
        if (heights[i] == std::numeric_limits<i32>::min()) continue;
        if (top_id[i] >= colors.size()) continue;
        auto c = colors[top_id[i]];
        if (c[3] == 0) continue;
        const f32 t = (heights[i] - hmin) / hspan;
        const f32 sh = 0.55f + 0.45f * t;
        m.rgba[i * 4 + 0] = static_cast<u8>(std::min(255.0f, c[0] * sh));
        m.rgba[i * 4 + 1] = static_cast<u8>(std::min(255.0f, c[1] * sh));
        m.rgba[i * 4 + 2] = static_cast<u8>(std::min(255.0f, c[2] * sh));
        m.rgba[i * 4 + 3] = c[3];
    }
    return m;
}

}
