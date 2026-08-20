#include "grid.hpp"

namespace vw::mesh {

VoxelGrid make_grid(std::span<const Chunk> chunks, const BBox& box) {
    VoxelGrid g;
    g.box = box;
    g.v.assign(static_cast<size_t>(box.sx()) * box.sy() * box.sz(), 0);
    bool any_biomes = false;
    for (const auto& ch : chunks) {
        if (any_biomes) break;
        for (const auto& s : ch.sections)
            if (s.biomes.size() >= 64) { any_biomes = true; break; }
    }
    if (any_biomes) g.biom.assign(g.v.size(), 0);
    for (const auto& ch : chunks) {
        const i32 bx = ch.cx * 16, bz = ch.cz * 16;
        if (bx > box.x1 || bx + 15 < box.x0 || bz > box.z1 || bz + 15 < box.z0) continue;
        for (const auto& sec : ch.sections) {
            const i32 by = sec.y * 16;
            if (by > box.y1 || by + 15 < box.y0) continue;
            for (int ly = 0; ly < 16; ++ly) {
                const i32 wy = by + ly;
                if (wy < box.y0 || wy > box.y1) continue;
                for (int lz = 0; lz < 16; ++lz) {
                    const i32 wz = bz + lz;
                    if (wz < box.z0 || wz > box.z1) continue;
                    for (int lx = 0; lx < 16; ++lx) {
                        const i32 wx = bx + lx;
                        if (wx < box.x0 || wx > box.x1) continue;
                        const u16 id = sec.at(lx, ly, lz);
                        const size_t i =
                            (static_cast<size_t>(wy - box.y0) * box.sz() + (wz - box.z0)) *
                                box.sx() + (wx - box.x0);
                        g.v[i] = id;
                        if (any_biomes)
                            g.biom[i] = sec.biome_at(lx >> 2, ly >> 2, lz >> 2);
                    }
                }
            }
        }
    }
    return g;
}

}
