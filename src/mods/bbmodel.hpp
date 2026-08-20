#pragma once
#include "core/types.hpp"
#include <span>
#include <string>
#include <vector>
#include <optional>

namespace vw::mods {

struct BBQuad {
    f32 p[4][3];
    f32 u[4], v[4];
    bool shade = true;
    std::string texture;
};

struct BBModel {
    int res_w = 16, res_h = 16;
    struct Cube {
        f32 from[3], to[3];
        f32 origin[3] = {0, 0, 0};
        f32 rot[3] = {0, 0, 0};
        f32 inflate = 0;
        bool shade = true;
        struct Face { int dir; f32 u0, v0, u1, v1; };
        std::vector<Face> faces;

        struct Group { f32 origin[3], rot[3]; };
        std::vector<Group> groups;
    };
    std::vector<Cube> cubes;
    bool has_root = false;
    f32 root_pos[3] = {0, 0, 0};
};

std::optional<BBModel> parse_bbmodel(std::span<const u8> bytes);

std::vector<BBQuad> bake_bbmodel(const BBModel& m, float scale, bool flip_v,
                                 const std::string& material);

}
