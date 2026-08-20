#pragma once
#include "core/types.hpp"
#include "mods/mod_assets.hpp"
#include "mods/bbmodel.hpp"
#include <array>
#include <mutex>

namespace vw::mods {

enum Face : int { FACE_WEST, FACE_EAST, FACE_DOWN, FACE_UP, FACE_NORTH, FACE_SOUTH };

struct ModelFace {
    bool present = false;
    std::string texture;
    f32 u0 = 0, v0 = 0, u1 = 16, v1 = 16;
    int uv_rot = 0;
    bool cullface = false;
    bool tint = false;
    bool shade = true;
};

struct ModelBox {
    f32 from[3] = {0, 0, 0};
    f32 to[3] = {16, 16, 16};
    ModelFace faces[6];

    bool has_rot = false;
    char rot_axis = 'y';
    f32 rot_angle = 0;
    f32 rot_origin[3] = {8, 8, 8};
    bool rot_rescale = false;

    int var_rx = 0, var_ry = 0;
    bool var_uvlock = false;
};

struct ResolvedModel {
    std::vector<ModelBox> boxes;

    std::vector<BBQuad> quads;
    int var_rx = 0, var_ry = 0;
    bool full_cube = false;
    bool cross = false;
    std::string cross_texture;
    bool empty() const { return boxes.empty() && quads.empty() && !cross; }
};

class ModelResolver {
public:
    explicit ModelResolver(AssetRegistry& assets) : assets_(assets) {}

    const ResolvedModel* resolve(const std::string& block_id);

    const ResolvedModel* resolve_state(const BlockState& state);

    void clear_cache();

private:
    ResolvedModel build(const BlockState& state);

    bool collect(const std::string& model_ref,
                 std::map<std::string, std::string, std::less<>>& texmap,
                 json::Value& elements_out, std::string& root_parent, int depth);
    std::string resolve_tex(const std::map<std::string, std::string, std::less<>>& texmap,
                            std::string ref) const;

    bool append_model(const std::string& model_ref, int rot_x, int rot_y,
                      bool uvlock, ResolvedModel& out);

    bool append_custom_model(const BlockState& state, const std::string& model_ref,
                             int rot_x, int rot_y, ResolvedModel& out);

    AssetRegistry& assets_;
    std::unordered_map<std::string, ResolvedModel> cache_;
    std::unordered_map<std::string, std::optional<BBModel>> bb_cache_;
    std::mutex mtx_;
};

}
