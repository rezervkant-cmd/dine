// Standalone debug harness: печатает квады (позиция+UV) для выбранных блоков,
// чтобы увидеть (а) правильность UV на люках/кострах и (б) задвоенные грани
// на растительности (cross).
#include "core/thread_pool.hpp"
#include "mods/mod_assets.hpp"
#include "mods/block_model.hpp"
#include "mesh/grid.hpp"
#include "render/mc_mesher.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace vw;
namespace fs = std::filesystem;

static BlockState BS(std::string name,
                     std::initializer_list<std::pair<std::string, std::string>> props) {
    BlockState b; b.name = std::move(name); b.props.assign(props);
    std::sort(b.props.begin(), b.props.end());
    return b;
}

int main(int argc, char** argv) {
    fs::path data = argc > 1 ? argv[1] : "tests/data";
    const char* block = argc > 2 ? argv[2] : "minecraft:tall_grass";

    ThreadPool pool;
    mods::AssetRegistry assets;
    assets.scan_jar(data / "vanilla" / "vanilla_test.jar");
    assets.scan_mods_dir(data / "mods", pool);
    mods::ModelResolver models(assets);

    GlobalPalette pal;
    auto P = [&](std::string n,
                 std::initializer_list<std::pair<std::string, std::string>> props) {
        return pal.intern(BS(std::move(n), props));
    };

    const u16 ID = [&]() -> u16 {
        if (std::string(block) == "minecraft:tall_grass")
            return P("minecraft:tall_grass", {{"half", "lower"}});
        if (std::string(block) == "minecraft:campfire")
            return P("minecraft:campfire", {{"facing","south"},{"lit","true"},{"signal_fire","false"},{"waterlogged","false"}});
        if (std::string(block) == "minecraft:oak_trapdoor")
            return P("minecraft:oak_trapdoor", {{"facing","east"},{"half","bottom"},{"open","true"},{"powered","false"},{"waterlogged","false"}});
        if (std::string(block) == "minecraft:torch")
            return P("minecraft:torch", {});
        if (std::string(block) == "minecraft:oak_trapdoor_closed")
            return P("minecraft:oak_trapdoor", {{"facing","east"},{"half","bottom"},{"open","false"},{"powered","false"},{"waterlogged","false"}});
        return P(block, {});
    }();

    mesh::TextureAtlas atlas;
    auto renders = render::build_block_renders(pal, assets, models, atlas, 0);
    atlas.bake();

    mesh::VoxelGrid grid;
    grid.box = {0, 0, 0, 0, 0, 0};
    grid.v = {ID};
    render::MCMeshOptions mopt;
    auto mesh = render::build_mc_mesh(grid, renders, atlas, mopt, &pool);

    std::printf("=== %s: %zu verts, %zu idx, %zu idx_alpha\n", block,
                mesh.vertices.size(), mesh.indices.size(), mesh.indices_alpha.size());

    auto dump = [&](const std::vector<u32>& idx, const char* tag) {
        for (size_t i = 0; i + 5 < idx.size(); i += 6) {
            const u32 q[4] = {idx[i], idx[i+1], idx[i+2], idx[i+5]};
            std::printf("[%s quad] tile-uv: ", tag);
            for (int k = 0; k < 4; ++k) {
                const auto& v = mesh.vertices[q[k]];
                // UV в локальных координатах тайла (0..16) по региону атласа
                std::printf("(%.3f,%.3f,%.3f|%.2f,%.2f) ", v.px, v.py, v.pz, v.u, v.v);
            }
            std::printf("\n");
        }
    };
    dump(mesh.indices, "opaque");
    dump(mesh.indices_alpha, "alpha");

    // Проверка копланарных дублей: квады с одинаковыми 4 позициями углов.
    struct Q { std::array<std::array<f32,3>,4> p; };
    std::vector<Q> qs;
    auto coll = [&](const std::vector<u32>& idx) {
        for (size_t i = 0; i + 5 < idx.size(); i += 6) {
            const u32 q[4] = {idx[i], idx[i+1], idx[i+2], idx[i+5]};
            Q e;
            for (int k = 0; k < 4; ++k) {
                const auto& v = mesh.vertices[q[k]];
                e.p[k] = {std::round(v.px*1000)/1000, std::round(v.py*1000)/1000, std::round(v.pz*1000)/1000};
            }
            qs.push_back(e);
        }
    };
    coll(mesh.indices);
    coll(mesh.indices_alpha);
    int dups = 0;
    for (size_t a = 0; a < qs.size(); ++a) {
        auto key = qs[a].p; std::sort(key.begin(), key.end());
        for (size_t b = a+1; b < qs.size(); ++b) {
            auto kb = qs[b].p; std::sort(kb.begin(), kb.end());
            if (key == kb) { ++dups; std::printf("DUP quad %zu == %zu\n", a, b); }
        }
    }
    std::printf("total quads=%zu, coplanar dups=%d\n", qs.size(), dups);
    return 0;
}
