#pragma once
#include "core/types.hpp"
#include "core/thread_pool.hpp"
#include "anvil/region.hpp"
#include <filesystem>
#include <mutex>

namespace vw::anvil {

struct WorldInfo {
    std::string name;
    std::string version_name;
    i32 data_version = 0;
    i32 spawn_x = 0, spawn_y = 64, spawn_z = 0;
};

class World {
public:

    explicit World(std::filesystem::path path);

    const WorldInfo& info() const { return info_; }
    GlobalPalette& palette() { return palette_; }
    const GlobalPalette& palette() const { return palette_; }
    size_t palette_size() const { return palette_.size(); }
    BlockState palette_get(u16 id) const { return palette_.get(id); }
    std::vector<BlockState> palette_snapshot() const { return palette_.snapshot(); }

    std::vector<std::string> biome_names() const {
        std::lock_guard lk(biome_mtx_);
        return biome_names_;
    }
    const std::vector<std::string> biome_names_copy() const { return biome_names(); }

    const std::vector<std::string>& dimensions() const { return dimensions_; }
    const std::string& dimension() const { return dimension_; }
    bool set_dimension(const std::string& name);

    std::vector<std::pair<i32, i32>> available_chunks() const;

    std::optional<Chunk> load_chunk(i32 cx, i32 cz);

    std::vector<Chunk> load_area(ThreadPool& pool, i32 cx0, i32 cz0, i32 cx1, i32 cz1);

    std::optional<Chunk> decode_chunk(const nbt::Document& doc, i32 cx, i32 cz);

private:
    RegionFile* region_for(i32 cx, i32 cz);

    std::filesystem::path root_;
    std::filesystem::path region_root_;
    std::vector<std::string> dimensions_;
    std::string dimension_ = "overworld";
    WorldInfo info_;
    GlobalPalette palette_;
    std::mutex palette_mtx_;
    std::vector<std::string> biome_names_;
    mutable std::mutex biome_mtx_;
    u8 biome_intern(const std::string& name);

    std::unordered_map<u64, std::unique_ptr<RegionFile>> regions_;
    std::mutex regions_mtx_;
};

}
