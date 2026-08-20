#pragma once
#include "core/types.hpp"
#include "mods/mod_assets.hpp"
#include <unordered_map>

namespace vw::mesh {

struct AtlasRegion {
    f32 u0, v0, u1, v1;
};

class TextureAtlas {
public:

    static constexpr int PAD = 4;

    int add(const std::string& name, const mods::TextureRGBA& tex);

    int add_solid(const std::string& name, std::array<u8, 4> rgba);

    bool has(const std::string& name) const { return index_.count(name) != 0; }
    int index_of(const std::string& name) const {
        auto it = index_.find(name);
        return it == index_.end() ? -1 : it->second;
    }

    size_t real_texture_count() const {
        size_t n = 0;
        for (auto& [k, v] : index_)
            if (!k.starts_with("avg:") && !k.starts_with("flat:") && !k.starts_with("__")) ++n;
        return n;
    }

    void bake();

    const AtlasRegion& region(int tile_index) const { return regions_[tile_index]; }
    int width() const { return w_; }
    int height() const { return h_; }
    const std::vector<u8>& pixels() const { return pixels_; }
    size_t tile_count() const { return tiles_.size(); }
    int tile_size() const { return tile_; }

    const std::string& tile_name(int tile_index) const {
        static const std::string kUnknown = "tile";
        return tile_index >= 0 && static_cast<size_t>(tile_index) < names_.size()
                   ? names_[static_cast<size_t>(tile_index)] : kUnknown;
    }

private:
    struct Tile { std::vector<u8> rgba; int w = 0, h = 0; };
    std::vector<Tile> tiles_;
    std::vector<std::string> names_;
    std::unordered_map<std::string, int> index_;
    std::vector<AtlasRegion> regions_;
    std::vector<u8> pixels_;
    int w_ = 0, h_ = 0, tile_ = 16;
};

}
