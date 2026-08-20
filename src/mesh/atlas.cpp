#include "atlas.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vw::mesh {

namespace {
void alpha_bleed(std::vector<u8>& px, int w, int h) {
    bool any_transparent = false;
    for (size_t i = 3; i < px.size(); i += 4)
        if (px[i] == 0) { any_transparent = true; break; }
    if (!any_transparent) return;
    constexpr int MAX_PASS = 8;
    std::vector<u8> cur = px;
    for (int pass = 0; pass < MAX_PASS; ++pass) {
        bool changed = false;
        std::vector<u8> nxt = cur;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const size_t i = (static_cast<size_t>(y) * w + x) * 4;
                if (cur[i + 3] != 0) continue;
                static const int kN[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
                int r = 0, g = 0, b = 0, n = 0;
                for (auto& [dx, dy] : kN) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    const size_t j = (static_cast<size_t>(ny) * w + nx) * 4;
                    if (cur[j + 3] == 0) continue;
                    r += cur[j]; g += cur[j + 1]; b += cur[j + 2]; ++n;
                }
                if (n) {
                    nxt[i] = static_cast<u8>(r / n);
                    nxt[i + 1] = static_cast<u8>(g / n);
                    nxt[i + 2] = static_cast<u8>(b / n);

                    changed = true;
                }
            }
        cur.swap(nxt);
        if (!changed) break;
    }
    px.swap(cur);
}
}

int TextureAtlas::add(const std::string& name, const mods::TextureRGBA& tex) {
    if (auto it = index_.find(name); it != index_.end()) return it->second;
    Tile t;
    t.w = tex.w;

    t.h = tex.animated ? std::min(tex.h, tex.w) : tex.h;
    t.rgba.assign(tex.pixels.begin(),
                  tex.pixels.begin() + static_cast<size_t>(t.w) * t.h * 4);
    alpha_bleed(t.rgba, t.w, t.h);
    const int id = static_cast<int>(tiles_.size());
    tiles_.push_back(std::move(t));
    names_.push_back(name);
    index_.emplace(name, id);
    return id;
}

int TextureAtlas::add_solid(const std::string& name, std::array<u8, 4> rgba) {
    if (auto it = index_.find(name); it != index_.end()) return it->second;
    Tile t;
    t.w = t.h = 16;
    t.rgba.resize(16 * 16 * 4);
    for (size_t i = 0; i < t.rgba.size(); i += 4) std::memcpy(&t.rgba[i], rgba.data(), 4);
    const int id = static_cast<int>(tiles_.size());
    tiles_.push_back(std::move(t));
    names_.push_back(name);
    index_.emplace(name, id);
    return id;
}

void TextureAtlas::bake() {
    if (tiles_.empty()) {
        add_solid("__missing__", {255, 0, 255, 255});
    }

    tile_ = 16;
    for (const auto& t : tiles_) tile_ = std::max(tile_, t.w);
    tile_ = std::min(tile_, 128);

    const int cell = tile_ + 2 * PAD;
    const int n = static_cast<int>(tiles_.size());
    const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n)))));
    const int rows = (n + cols - 1) / cols;
    w_ = cols * cell;
    h_ = rows * cell;
    pixels_.assign(static_cast<size_t>(w_) * h_ * 4, 0);
    regions_.resize(n);

    for (int i = 0; i < n; ++i) {
        const auto& t = tiles_[i];
        const int cx = (i % cols) * cell, cy = (i / cols) * cell;

        auto sample = [&](int x, int y) -> const u8* {
            const int sx = std::clamp(x * t.w / tile_, 0, t.w - 1);
            const int sy = std::clamp(y * t.h / tile_, 0, t.h - 1);
            return &t.rgba[(static_cast<size_t>(sy) * t.w + sx) * 4];
        };

        for (int y = -PAD; y < tile_ + PAD; ++y)
            for (int x = -PAD; x < tile_ + PAD; ++x) {
                const u8* src = sample(std::clamp(x, 0, tile_ - 1),
                                       std::clamp(y, 0, tile_ - 1));
                u8* dst = &pixels_[((static_cast<size_t>(cy + PAD + y)) * w_ +
                                    (cx + PAD + x)) * 4];
                std::memcpy(dst, src, 4);
            }
        regions_[i] = {
            static_cast<f32>(cx + PAD) / w_,
            static_cast<f32>(cy + PAD) / h_,
            static_cast<f32>(cx + PAD + tile_) / w_,
            static_cast<f32>(cy + PAD + tile_) / h_,
        };
    }
}

}
