#pragma once
#include "core/types.hpp"
#include "core/biome_colors.hpp"

namespace vw::mesh {

// Воксельная сетка выбранной области мира: id блока и (опционально) биом.
struct VoxelGrid {
    BBox box;
    std::vector<u16> v;
    std::vector<u8> biom;                    // 0, если биомов нет; индекс в biome_pal
    std::vector<BiomeColors> biome_pal;      // цвета по глобальному id биома

    u16 at(i32 x, i32 y, i32 z) const {
        if (x < box.x0 || x > box.x1 || y < box.y0 || y > box.y1 ||
            z < box.z0 || z > box.z1) return 0;
        const size_t i = (static_cast<size_t>(y - box.y0) * box.sz() +
                          (z - box.z0)) * box.sx() + (x - box.x0);
        return v[i];
    }
    u8 biome(i32 x, i32 y, i32 z) const {
        if (biom.empty()) return 0;
        if (x < box.x0 || x > box.x1 || y < box.y0 || y > box.y1 ||
            z < box.z0 || z > box.z1) return 0;
        const size_t i = (static_cast<size_t>(y - box.y0) * box.sz() +
                          (z - box.z0)) * box.sx() + (x - box.x0);
        return biom[i];
    }
};

// Заполняет сетку из загруженных чанков.
VoxelGrid make_grid(std::span<const Chunk> chunks, const BBox& box);

}
