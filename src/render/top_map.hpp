#pragma once
#include "core/types.hpp"
#include "core/thread_pool.hpp"
#include "anvil/world.hpp"
#include "core/block_colors.hpp"
#include "mods/mod_assets.hpp"
#include <vector>

namespace vw::render {

struct TopMap {
    i32 x0 = 0, z0 = 0;
    i32 w = 0, h = 0;
    i32 y_max = 319;
    i32 y_min = 0;
    i32 y_peak = 0;
    std::vector<u8> rgba;
    bool empty() const { return rgba.empty(); }
};

TopMap build_top_map(anvil::World& world,
                     const mods::AssetRegistry* assets,
                     i32 x0, i32 z0, i32 x1, i32 z1,
                     i32 y_max, ThreadPool* pool = nullptr);

std::array<u8, 4> top_map_color(const BlockState& bs, const mods::AssetRegistry* assets);

}
