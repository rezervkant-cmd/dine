#include "core/thread_pool.hpp"
#include "mods/mod_assets.hpp"
#include "mods/block_model.hpp"
#include "mods/json.hpp"
#include <algorithm>
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
    mods::AssetRegistry assets;
    assets.scan_jar(data / "vanilla" / "vanilla_test.jar");

    const char* texs[] = {
        "minecraft:block/campfire_log", "minecraft:block/campfire_log_lit",
        "minecraft:block/campfire_fire", "minecraft:block/soul_campfire_fire",
        "minecraft:block/soul_campfire_log_lit", "minecraft:block/oak_planks",
        "minecraft:block/cobblestone", "minecraft:block/tnt_side",
        "minecraft:block/cake_side", "minecraft:block/iron_bars",
        "minecraft:block/stone", "minecraft:block/lantern",
    };
    for (const char* t : texs) {
        if (!assets.has_texture(t)) { std::printf("%-40s NOT IN JAR\n", t); continue; }
        auto raw = assets.raw_texture_png(t);
        auto tex = assets.load_texture(t);
        std::printf("%-40s raw=%zu dec=%s", t, raw ? raw->size() : 0,
                    tex ? "OK" : "FAIL");
        if (tex) {
            std::printf(" %dx%d px0=(%d,%d,%d,%d)", tex->w, tex->h,
                        tex->pixels[0], tex->pixels[1], tex->pixels[2], tex->pixels[3]);
        }
        std::printf("\n");

        if (raw && raw->size() > 33) {
            const u8* p = raw->data() + 16;
            unsigned w = (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
            unsigned h = (p[4]<<24)|(p[5]<<16)|(p[6]<<8)|p[7];
            std::printf("  IHDR: %ux%u bit=%d color=%d interlace=%d\n", w, h,
                        raw->data()[24], raw->data()[25], raw->data()[28]);
        }
    }

    mods::ModelResolver models(assets);
    auto dump = [&](const BlockState& bs) {
        std::printf("\n=== %s\n", bs.key().c_str());
        auto* rm = models.resolve_state(bs);
        if (!rm) { std::printf("  NULL MODEL\n"); return; }
        std::printf("  boxes=%zu cross=%d full=%d\n", rm->boxes.size(), rm->cross, rm->full_cube);
        for (size_t i = 0; i < rm->boxes.size(); ++i) {
            const auto& b = rm->boxes[i];
            std::printf("  box%zu from=(%.1f,%.1f,%.1f) to=(%.1f,%.1f,%.1f) rot=%d ax=%c ang=%.1f resc=%d var=(%d,%d) lock=%d\n",
                        i, b.from[0], b.from[1], b.from[2], b.to[0], b.to[1], b.to[2],
                        b.has_rot, b.rot_axis, b.rot_angle, b.rot_rescale,
                        b.var_rx, b.var_ry, b.var_uvlock);
            for (int f = 0; f < 6; ++f) {
                const auto& mf = b.faces[f];
                if (!mf.present) continue;
                std::printf("    f%d tex=%s uv=(%.0f,%.0f,%.0f,%.0f) uvr=%d cull=%d shade=%d\n",
                            f, mf.texture.c_str(), mf.u0, mf.v0, mf.u1, mf.v1, mf.uv_rot,
                            mf.cullface, mf.shade);
            }
        }
    };
    dump(BS("minecraft:campfire", {{"facing","north"},{"lit","true"},{"signal_fire","false"},{"waterlogged","false"}}));
    dump(BS("minecraft:oak_fence", {{"east","true"},{"north","false"},{"south","false"},{"waterlogged","false"},{"west","true"}}));
    dump(BS("minecraft:oak_stairs", {{"facing","east"},{"half","bottom"},{"shape","straight"},{"waterlogged","false"}}));
    dump(BS("minecraft:cobblestone_wall", {{"east","true"},{"north","false"},{"south","false"},{"up","true"},{"waterlogged","false"},{"west","true"}}));
    dump(BS("minecraft:tnt", {{"unstable","false"}}));
    dump(BS("minecraft:iron_bars", {{"east","true"},{"north","false"},{"south","false"},{"waterlogged","false"},{"west","true"}}));
    return 0;
}
