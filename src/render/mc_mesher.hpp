#pragma once
#include "core/types.hpp"
#include "core/thread_pool.hpp"
#include "mesh/grid.hpp"
#include "mesh/atlas.hpp"
#include "mods/block_model.hpp"

namespace vw::render {

struct MCVertex {
    f32 px, py, pz;
    f32 u, v;
    f32 r, g, b;
    f32 nx, ny, nz;
};

struct MCMesh {
    std::vector<MCVertex> vertices;
    std::vector<u32> indices;
    std::vector<u32> indices_alpha;
    std::vector<u32> quads_by_block;
};

struct BlockRender {
    struct FaceTex {
        int tile = -1;
        f32 u0 = 0, v0 = 0, u1 = 16, v1 = 16;
        int uv_rot = 0;
        bool cull = false;
        bool tint = false;
        bool shade = true;
    };
    struct Box {
        f32 from[3], to[3];
        FaceTex faces[6];

        bool has_rot = false;
        char rot_axis = 'y';
        f32 rot_angle = 0;
        f32 rot_origin[3] = {8, 8, 8};
        bool rot_rescale = false;

        int var_rx = 0, var_ry = 0;
        bool var_uvlock = false;
    };
    std::vector<Box> boxes;

    struct Quad {
        f32 p[4][3];
        f32 u[4], v[4];
        int tile = -1;
        bool shade = true;
    };
    std::vector<Quad> quads;
    int var_rx = 0, var_ry = 0;
    bool cross = false;
    int cross_tile = -1;
    bool full_opaque_cube = false;   // геометрически полный непрозрачный куб
    bool foliage = false;            // листва: cutout-текстура, соседей не перекрывает
    bool alpha_blend = false;
    bool fluid = false;
    u8 fluid_kind = 0;               // 0 = нет, 1 = вода, 2 = лава
    f32 fluid_top = -1.0f;           // высота верха жидкости в 1/16 блока (-1 = не жидкость)
    bool waterlogged = false;        // в ячейке есть вода (waterlogged)
    u8 tint_family = 0;              // 1 трава, 2 листва, 3 вода (биом-тонировка)
    bool decor = false;
    bool bedrock = false;
    bool barrier = false;
    f32 tint[3] = {1, 1, 1};
    bool empty() const { return boxes.empty() && quads.empty() && !cross; }
};

std::vector<BlockRender> build_block_renders(const GlobalPalette& palette,
                                             mods::AssetRegistry& assets,
                                             mods::ModelResolver& models,
                                             mesh::TextureAtlas& atlas,
                                             int texture_mode);

struct MCMeshOptions {
    bool skip_fluids = false;
    bool skip_decor = false;
    bool skip_bedrock = false;
    bool skip_barriers = false;

    // «Густая» листва: блоки листвы не считаются окклюдерами, поэтому
    // внутренние грани кроны переживают генерацию меша (режим cutout).
    bool fancy_leaves = true;
};

MCMesh build_mc_mesh(const mesh::VoxelGrid& grid,
                     const std::vector<BlockRender>& renders,
                     const mesh::TextureAtlas& atlas,
                     const MCMeshOptions& opt,
                     ThreadPool* pool = nullptr);

}
