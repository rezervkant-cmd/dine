#pragma once
#include "core/types.hpp"
#include "nbt/nbt.hpp"
#include <filesystem>

namespace vw::anvil {

class RegionFile {
public:

    explicit RegionFile(const std::filesystem::path& path);

    bool has_chunk(int lx, int lz) const;

    std::optional<nbt::Document> read_chunk(int lx, int lz) const;

    i32 region_x() const { return rx_; }
    i32 region_z() const { return rz_; }

private:
    std::vector<u8> data_;
    i32 rx_ = 0, rz_ = 0;
};

}
