#pragma once
#include "render/mc_mesher.hpp"
#include <filesystem>
#include <map>

namespace vw::exporter {

struct MCExportContext {
    const render::MCMesh* mesh = nullptr;
    const mesh::TextureAtlas* atlas = nullptr;
    f32 scale = 1.0f;
    const std::vector<render::BlockRender>* renders = nullptr;
};

// Общая подготовка экспорта для OBJ и GLB (сварка вершин, дедупликация квадов).
struct MCBaked {
    struct Quad {
        int tile;
        u32 w[4];
    };
    const render::MCMesh* mesh = nullptr;
    f32 scale = 1.0f;
    std::vector<u32> vert_src;            // сваренные вершины -> индекс в mesh->vertices
    std::vector<Quad> quads;              // сгруппированы по тайлу (возрастание)
    std::map<int, std::string> mtl_name;  // tile -> имя материала
    std::map<int, int> tile_alpha;        // tile -> 0 opaque / 1 cutout / 2 translucent
    std::vector<u8> atlas_pixels;         // RGBA копия атласа с биом-тоном
    int atlas_w = 0, atlas_h = 0;
};

bool bake_mc_export(const MCExportContext& ctx, MCBaked& out);

// Wavefront OBJ + MTL + отдельные PNG-тайлы текстур.
bool export_mc_obj(const MCExportContext& ctx, const std::filesystem::path& obj_path);
bool export_mc_glb(const MCExportContext& ctx, const std::filesystem::path& glb_path);

// Экспорт мира отдельными 64x64-плитками (для Blender).
size_t export_mc_glb_chunks(const MCExportContext& ctx, const std::filesystem::path& directory,
                            const std::string& base_name, i32 chunk_blocks = 64);

std::vector<u8> encode_png(const u8* rgba, int w, int h);

}
