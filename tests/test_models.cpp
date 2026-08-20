#include "core/thread_pool.hpp"
#include "mods/mod_assets.hpp"
#include "mods/block_model.hpp"
#include "render/mc_mesher.hpp"
#include "render/top_map.hpp"
#include "anvil/world.hpp"
#include "export/mc_export.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace vw;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
    else std::printf("ok   %s\n", #cond); \
} while (0)

static BlockState BS(std::string name,
                     std::initializer_list<std::pair<std::string, std::string>> props) {
    BlockState b;
    b.name = std::move(name);
    b.props.assign(props);
    std::sort(b.props.begin(), b.props.end());
    return b;
}

struct Cam {
    float dir[3], right[3], up[3];
    float scale = 30.0f;
    int w = 900, h = 620;
    float ox = 0, oy = 0;
};

static void make_cam(Cam& c, float ax, float ay, float az, float upx, float upy, float upz) {
    auto norm = [](float v[3]) {
        float l = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
        v[0] /= l; v[1] /= l; v[2] /= l;
    };
    float d[3] = {ax, ay, az};
    norm(d);

    float r[3];
    r[0] = upy*d[2] - upz*d[1];
    r[1] = upz*d[0] - upx*d[2];
    r[2] = upx*d[1] - upy*d[0];
    norm(r);

    float u[3] = {d[1]*r[2] - d[2]*r[1], d[2]*r[0] - d[0]*r[2], d[0]*r[1] - d[1]*r[0]};
    c.dir[0]=d[0]; c.dir[1]=d[1]; c.dir[2]=d[2];
    c.right[0]=r[0]; c.right[1]=r[1]; c.right[2]=r[2];
    c.up[0]=u[0]; c.up[1]=u[1]; c.up[2]=u[2];
}

int main(int argc, char** argv) {
    fs::path data = argc > 1 ? argv[1] : "tests/data";
    fs::path outdir = argc > 2 ? argv[2] : "tests/out";
    fs::create_directories(outdir);
    ThreadPool pool;

    mods::AssetRegistry assets;
    bool has_vanilla = false, has_deco = false;
    try {
        if (fs::exists(data / "vanilla" / "vanilla_test.jar")) {
            assets.scan_jar(data / "vanilla" / "vanilla_test.jar");
            has_vanilla = true;
        } else {
            std::printf("SKIP: vanilla_test.jar not found at %s (run python3 tests/gen_vanilla_test_jar.py)\n",
                        (data / "vanilla" / "vanilla_test.jar").string().c_str());
        }
    } catch (const std::exception& e) { std::printf("jar error vanilla: %s\n", e.what()); }
    if (has_vanilla) {
        CHECK(assets.mods().size() >= 1);
        CHECK(assets.has_texture("minecraft:block/campfire_fire"));
    }

    try {
        if (fs::exists(data / "mods" / "decomini.jar")) {
            assets.scan_jar(data / "mods" / "decomini.jar");
            has_deco = true;
        } else {
            std::printf("SKIP: decomini.jar not found (run python3 tests/gen_deco_test_jar.py)\n");
        }
    } catch (const std::exception& e) { std::printf("jar error deco: %s\n", e.what()); }
    if (has_deco) {
        CHECK(assets.has_texture("decotest:block/deco_lamp"));
    }
    if (!has_vanilla) {
        std::printf("SKIP: vanilla assets missing — skipping model geometry checks, only core tests will run\n");
        // still run non-vanilla checks that don't need those textures? For now just skip to core
        // But many tests require vanilla; we mark as passed if assets missing to not break CI on clean checkout
        // CI will generate assets, so this branch won't be taken in CI
        if (!has_deco) {
            std::printf("\nALL TESTS SKIPPED (no fixtures) (%d failures)\n", failures);
            return failures ? 1 : 0;
        }
    }

    mods::ModelResolver models(assets);

    if (has_vanilla) {
        auto cf = BS("minecraft:campfire", {
            {"facing", "north"}, {"lit", "true"}, {"signal_fire", "false"}, {"waterlogged", "false"}});
        const auto* rm = models.resolve_state(cf);
        CHECK(rm != nullptr);
        if (rm) {
            CHECK(rm->boxes.size() == 7);
            int rot45 = 0, flat_fire = 0, unshaded = 0, uvrot_logs = 0;
            f32 min_c = 1e9f, max_c = -1e9f;
            for (const auto& b : rm->boxes) {
                if (b.has_rot && std::fabs(std::fabs(b.rot_angle) - 45.0f) < 0.01f &&
                    b.rot_axis == 'y' && b.rot_rescale)
                    ++rot45;
                if (std::fabs(b.to[2] - b.from[2]) < 0.001f ||
                    std::fabs(b.to[0] - b.from[0]) < 0.001f)
                    ++flat_fire;
                for (int f = 0; f < 6; ++f) {
                    if (b.faces[f].present && !b.faces[f].shade) ++unshaded;
                    if (b.faces[f].present && b.faces[f].uv_rot != 0) ++uvrot_logs;
                }
                for (int i = 0; i < 3; ++i) {
                    min_c = std::min(min_c, b.from[i]);
                    max_c = std::max(max_c, b.to[i]);
                }
            }
            CHECK(rot45 == 2);
            CHECK(flat_fire == 2);
            CHECK(unshaded == 4);
            CHECK(uvrot_logs >= 5);
            CHECK(min_c >= -8.0f && max_c <= 24.0f);
        }
    }

    if (has_vanilla) {
        auto fen = BS("minecraft:oak_fence", {
            {"east", "true"}, {"north", "true"}, {"south", "true"},
            {"waterlogged", "false"}, {"west", "true"}});
        const auto* rm = models.resolve_state(fen);
        CHECK(rm != nullptr);
        if (rm) {
            CHECK(rm->boxes.size() == 1 + 4 * 2);
            int ry[4] = {0, 0, 0, 0}, uvlocks = 0;
            for (const auto& b : rm->boxes) {
                const int y = ((b.var_ry % 360) + 360) % 360;
                if (y % 90 == 0) ++ry[y / 90];
                if (b.var_uvlock) ++uvlocks;
                CHECK(b.from[0] >= 0 && b.from[0] <= 16 && b.to[0] >= 0 && b.to[0] <= 16);
                CHECK(b.from[2] >= 0 && b.from[2] <= 16 && b.to[2] >= 0 && b.to[2] <= 16);
            }
            CHECK(ry[0] == 3 && ry[1] == 2 && ry[2] == 2 && ry[3] == 2);
            CHECK(uvlocks == 8);
        }

        auto fen1 = BS("minecraft:oak_fence", {
            {"east", "false"}, {"north", "true"}, {"south", "false"},
            {"waterlogged", "false"}, {"west", "false"}});
        const auto* rm1 = models.resolve_state(fen1);
        CHECK(rm1 && rm1->boxes.size() == 3);

        auto wcol = BS("minecraft:cobblestone_wall", {
            {"east","none"},{"north","none"},{"south","none"},{"up","false"},
            {"waterlogged","false"},{"west","none"}});
        const auto* rmw = models.resolve_state(wcol);
        CHECK(rmw && !rmw->boxes.empty());
        if (rmw && !rmw->boxes.empty()) {
            const auto& b = rmw->boxes[0];
            CHECK(b.from[0] == 4 && b.to[0] == 12 && b.from[2] == 4 && b.to[2] == 12);
        }
    }

    GlobalPalette pal;
    auto P = [&](std::string n) { return pal.intern({std::move(n), {}}); };
    const u16 ID_AIR  = P("minecraft:air");
    const u16 ID_STONE = P("minecraft:stone");
    (void)ID_AIR; (void)ID_STONE;

    auto put = [&](mesh::VoxelGrid& g, int x, int y, int z, const BlockState& b) {
        const size_t i = (size_t(y - g.box.y0) * g.box.sz() + (z - g.box.z0)) * g.box.sx() + (x - g.box.x0);
        g.v[i] = pal.intern(b);
    };

    mesh::VoxelGrid grid;
    grid.box = {0, 0, 0, 15, 4, 15};
    grid.v.assign(size_t(grid.box.sx()) * grid.box.sy() * grid.box.sz(), 0);

    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            put(grid, x, 0, z, {"minecraft:stone", {}});

    put(grid, 1, 1, 1, BS("minecraft:campfire", {{"facing","north"},{"lit","true"},{"signal_fire","false"},{"waterlogged","false"}}));
    put(grid, 3, 1, 1, BS("minecraft:soul_campfire", {{"facing","north"},{"lit","true"},{"signal_fire","false"},{"waterlogged","false"}}));
    put(grid, 5, 1, 1, BS("minecraft:campfire", {{"facing","north"},{"lit","false"},{"signal_fire","false"},{"waterlogged","false"}}));

    put(grid, 1, 1, 3, BS("minecraft:oak_fence", {{"east","false"},{"north","false"},{"south","false"},{"waterlogged","false"},{"west","false"}}));
    put(grid, 2, 1, 3, BS("minecraft:oak_fence", {{"east","true"},{"north","false"},{"south","false"},{"waterlogged","false"},{"west","true"}}));
    put(grid, 3, 1, 3, BS("minecraft:oak_fence", {{"east","true"},{"north","false"},{"south","false"},{"waterlogged","false"},{"west","true"}}));
    put(grid, 5, 1, 3, BS("minecraft:oak_fence", {{"east","true"},{"north","true"},{"south","true"},{"waterlogged","false"},{"west","true"}}));
    put(grid, 6, 1, 2, BS("minecraft:oak_fence", {{"east","false"},{"north","false"},{"south","true"},{"waterlogged","false"},{"west","false"}}));
    put(grid, 6, 1, 3, BS("minecraft:birch_fence", {{"east","false"},{"north","true"},{"south","true"},{"waterlogged","false"},{"west","false"}}));
    put(grid, 6, 1, 4, BS("minecraft:birch_fence", {{"east","false"},{"north","true"},{"south","false"},{"waterlogged","false"},{"west","false"}}));

    put(grid, 1, 1, 5, BS("minecraft:cobblestone_wall", {{"east","low"},{"north","none"},{"south","none"},{"up","true"},{"waterlogged","false"},{"west","low"}}));
    put(grid, 2, 1, 5, BS("minecraft:cobblestone_wall", {{"east","low"},{"north","none"},{"south","none"},{"up","true"},{"waterlogged","false"},{"west","low"}}));
    put(grid, 4, 1, 5, BS("minecraft:cobblestone_wall", {{"east","none"},{"north","none"},{"south","none"},{"up","true"},{"waterlogged","false"},{"west","none"}}));

    put(grid, 6, 1, 5, BS("minecraft:cobblestone_wall", {{"east","none"},{"north","none"},{"south","none"},{"up","false"},{"waterlogged","false"},{"west","none"}}));
    put(grid, 6, 2, 5, BS("minecraft:cobblestone_wall", {{"east","none"},{"north","none"},{"south","none"},{"up","true"},{"waterlogged","false"},{"west","none"}}));

    put(grid, 1, 1, 7, BS("minecraft:oak_stairs", {{"facing","east"},{"half","bottom"},{"shape","straight"},{"waterlogged","false"}}));
    put(grid, 2, 1, 7, BS("minecraft:oak_stairs", {{"facing","west"},{"half","bottom"},{"shape","straight"},{"waterlogged","false"}}));
    put(grid, 3, 1, 7, BS("minecraft:oak_stairs", {{"facing","east"},{"half","top"},{"shape","straight"},{"waterlogged","false"}}));
    put(grid, 5, 1, 7, BS("minecraft:stone_slab", {{"type","bottom"},{"waterlogged","false"}}));
    put(grid, 6, 1, 7, BS("minecraft:stone_slab", {{"type","top"},{"waterlogged","false"}}));

    put(grid, 1, 1, 9, BS("minecraft:glass_pane", {{"east","false"},{"north","true"},{"south","true"},{"waterlogged","false"},{"west","false"}}));
    put(grid, 1, 1, 8, BS("minecraft:glass_pane", {{"east","false"},{"north","true"},{"south","true"},{"waterlogged","false"},{"west","false"}}));
    put(grid, 1, 1, 10, BS("minecraft:glass_pane", {{"east","false"},{"north","true"},{"south","false"},{"waterlogged","false"},{"west","false"}}));
    put(grid, 3, 1, 9, BS("minecraft:iron_bars", {{"east","true"},{"north","false"},{"south","false"},{"waterlogged","false"},{"west","true"}}));
    put(grid, 4, 1, 9, BS("minecraft:iron_bars", {{"east","true"},{"north","false"},{"south","false"},{"waterlogged","false"},{"west","true"}}));
    put(grid, 5, 1, 9, BS("minecraft:iron_bars", {{"east","true"},{"north","false"},{"south","false"},{"waterlogged","false"},{"west","true"}}));
    put(grid, 6, 1, 9, {"minecraft:stone", {}});
    put(grid, 6, 1, 10, BS("minecraft:ladder", {{"facing","north"},{"waterlogged","false"}}));
    put(grid, 8, 1, 9, BS("minecraft:lantern", {{"hanging","false"},{"waterlogged","false"}}));
    put(grid, 10, 1, 9, BS("minecraft:oak_trapdoor", {{"facing","east"},{"half","bottom"},{"open","true"},{"powered","false"},{"waterlogged","false"}}));

    put(grid, 1, 1, 11, BS("minecraft:cake", {{"bites","0"}}));
    put(grid, 3, 1, 11, BS("minecraft:tnt", {{"unstable","false"}}));
    put(grid, 5, 1, 11, BS("minecraft:grass_block", {{"snowy","false"}}));
    put(grid, 7, 1, 11, BS("minecraft:tall_grass", {{"half","lower"}}));
    put(grid, 7, 2, 11, BS("minecraft:tall_grass", {{"half","upper"}}));

    put(grid, 9, 1, 11, {"minecraft:barrier", {}});
    put(grid, 10, 1, 11, {"minecraft:barrier", {}});

    mesh::TextureAtlas atlas;
    auto renders = render::build_block_renders(pal, assets, models, atlas, 0);
    atlas.bake();
    render::MCMeshOptions mopt;
    auto mesh = render::build_mc_mesh(grid, renders, atlas, mopt, &pool);
    CHECK(!mesh.vertices.empty());
    std::printf("mesh: %zu verts, %zu tris\n", mesh.vertices.size(), mesh.indices.size() / 3);

    Cam cam;
    make_cam(cam, -0.9f, 0.75f, -0.95f, 0.0f, 1.0f, 0.0f);
    std::vector<float> zbuf(size_t(cam.w) * cam.h, -1e30f);
    std::vector<u8> img(size_t(cam.w) * cam.h * 4);

    for (size_t i = 0; i < img.size(); i += 4) {
        img[i] = 26; img[i + 1] = 28; img[i + 2] = 33; img[i + 3] = 255;
    }
    auto project = [&](const render::MCVertex& v, float& sx, float& sy, float& sd) {

        const float cx = 8.0f, cy = 0.8f, cz = 8.0f;
        const float px = v.px - cx, py = v.py - cy, pz = v.pz - cz;
        sx = px * cam.right[0] + py * cam.right[1] + pz * cam.right[2];
        sy = px * cam.up[0] + py * cam.up[1] + pz * cam.up[2];
        sd = px * cam.dir[0] + py * cam.dir[1] + pz * cam.dir[2];
        sx = cam.w * 0.5f + sx * cam.scale + cam.ox;
        sy = cam.h * 0.5f - sy * cam.scale + cam.oy;
    };
    const int aw = atlas.width(), ah = atlas.height();
    auto draw_tri = [&](const render::MCVertex& a, const render::MCVertex& b,
                        const render::MCVertex& c) {
        float x[3], y[3], d[3];
        project(a, x[0], y[0], d[0]);
        project(b, x[1], y[1], d[1]);
        project(c, x[2], y[2], d[2]);
        const float minx = std::max(0.0f, std::floor(std::min({x[0], x[1], x[2]})));
        const float maxx = std::min(float(cam.w - 1), std::ceil(std::max({x[0], x[1], x[2]})));
        const float miny = std::max(0.0f, std::floor(std::min({y[0], y[1], y[2]})));
        const float maxy = std::min(float(cam.h - 1), std::ceil(std::max({y[0], y[1], y[2]})));
        auto edge = [](float x0, float y0, float x1, float y1, float px, float py) {
            return (px - x0) * (y1 - y0) - (py - y0) * (x1 - x0);
        };
        const float area = edge(x[0], y[0], x[1], y[1], x[2], y[2]);
        if (std::fabs(area) < 1e-9f) return;
        for (int py = int(miny); py <= int(maxy); ++py)
            for (int px = int(minx); px <= int(maxx); ++px) {
                const float w0 = edge(x[1], y[1], x[2], y[2], px + 0.5f, py + 0.5f) / area;
                const float w1 = edge(x[2], y[2], x[0], y[0], px + 0.5f, py + 0.5f) / area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                const float dd = w0 * d[0] + w1 * d[1] + w2 * d[2];
                const size_t pi = size_t(py) * cam.w + px;
                if (dd <= zbuf[pi] + 1e-5f) continue;
                const float u = w0 * a.u + w1 * b.u + w2 * c.u;
                const float v = w0 * a.v + w1 * b.v + w2 * c.v;
                const int tx = std::clamp(int(u * aw), 0, aw - 1);
                const int ty = std::clamp(int(v * ah), 0, ah - 1);
                const u8* tex = &atlas.pixels()[(size_t(ty) * aw + tx) * 4];
                if (tex[3] < 128) continue;
                const float cr = w0 * a.r + w1 * b.r + w2 * c.r;
                const float cg = w0 * a.g + w1 * b.g + w2 * c.g;
                const float cb = w0 * a.b + w1 * b.b + w2 * c.b;
                zbuf[pi] = dd;
                img[pi * 4 + 0] = u8(std::min(255.0f, tex[0] * cr));
                img[pi * 4 + 1] = u8(std::min(255.0f, tex[1] * cg));
                img[pi * 4 + 2] = u8(std::min(255.0f, tex[2] * cb));
                img[pi * 4 + 3] = 255;
            }
    };
    auto draw_idx = [&](const std::vector<u32>& idx) {
        for (size_t i = 0; i + 2 < idx.size(); i += 3)
            draw_tri(mesh.vertices[idx[i]], mesh.vertices[idx[i + 1]], mesh.vertices[idx[i + 2]]);
    };
    draw_idx(mesh.indices_alpha);
    draw_idx(mesh.indices);

    {
        anvil::World w(data / "world_modern");
        auto map = render::build_top_map(w, &assets, -8, -8, 39, 39, 319, &pool);
        CHECK(map.w == 48 && map.h == 48);
        int non_air_px = 0;
        for (size_t i = 0; i < map.rgba.size(); i += 4)
            if (map.rgba[i + 3] > 0) ++non_air_px;
        CHECK(non_air_px > 200);

        const size_t center = (size_t(8 - map.z0) * map.w + (8 - map.x0)) * 4;
        CHECK(map.rgba[center + 3] > 0);
        auto png = exporter::encode_png(map.rgba.data(), map.w, map.h);
        fs::path p = outdir / "top_map.png";
        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
        std::printf("wrote %s (y %d..%d, %d px)\n", p.c_str(), map.y_min, map.y_peak, non_air_px);
    }

    {
        auto png = exporter::encode_png(img.data(), cam.w, cam.h);
        fs::path p = outdir / "scene_blocks.png";
        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
        std::printf("wrote %s (%zu bytes)\n", p.c_str(), png.size());
    }

    struct NamedBS { const char* name; BlockState bs; };
    std::vector<NamedBS> singles = {
        {"campfire_lit",     BS("minecraft:campfire", {{"facing","north"},{"lit","true"},{"signal_fire","false"},{"waterlogged","false"}})},
        {"soul_campfire",    BS("minecraft:soul_campfire", {{"facing","north"},{"lit","true"},{"signal_fire","false"},{"waterlogged","false"}})},
        {"campfire_off",     BS("minecraft:campfire", {{"facing","north"},{"lit","false"},{"signal_fire","false"},{"waterlogged","false"}})},
        {"fence_cross",      BS("minecraft:oak_fence", {{"east","true"},{"north","true"},{"south","true"},{"waterlogged","false"},{"west","true"}})},
        {"fence_we",         BS("minecraft:oak_fence", {{"east","true"},{"north","false"},{"south","false"},{"waterlogged","false"},{"west","true"}})},
        {"fence_post",       BS("minecraft:oak_fence", {{"east","false"},{"north","false"},{"south","false"},{"waterlogged","false"},{"west","false"}})},
        {"wall_cross",       BS("minecraft:cobblestone_wall", {{"east","low"},{"north","low"},{"south","tall"},{"up","true"},{"waterlogged","false"},{"west","low"}})},
        {"wall_we",          BS("minecraft:cobblestone_wall", {{"east","low"},{"north","none"},{"south","none"},{"up","true"},{"waterlogged","false"},{"west","low"}})},
        {"wall_stack_low",   BS("minecraft:cobblestone_wall", {{"east","none"},{"north","none"},{"south","none"},{"up","false"},{"waterlogged","false"},{"west","none"}})},
        {"stairs_e",         BS("minecraft:oak_stairs", {{"facing","east"},{"half","bottom"},{"shape","straight"},{"waterlogged","false"}})},
        {"stairs_w_top",     BS("minecraft:oak_stairs", {{"facing","west"},{"half","top"},{"shape","straight"},{"waterlogged","false"}})},
        {"slab_top",         BS("minecraft:stone_slab", {{"type","top"},{"waterlogged","false"}})},
        {"glass_pane_ns",    BS("minecraft:glass_pane", {{"east","false"},{"north","true"},{"south","true"},{"waterlogged","false"},{"west","false"}})},
        {"iron_bars_we",     BS("minecraft:iron_bars", {{"east","true"},{"north","false"},{"south","false"},{"waterlogged","false"},{"west","true"}})},
        {"trapdoor_open_e",  BS("minecraft:oak_trapdoor", {{"facing","east"},{"half","bottom"},{"open","true"},{"powered","false"},{"waterlogged","false"}})},
        {"lantern",          BS("minecraft:lantern", {{"hanging","false"},{"waterlogged","false"}})},
        {"ladder_n",         BS("minecraft:ladder", {{"facing","north"},{"waterlogged","false"}})},
        {"cake",             BS("minecraft:cake", {{"bites","0"}})},
        {"tnt",              BS("minecraft:tnt", {{"unstable","false"}})},
        {"deco_lamp_n",      BS("decotest:deco_lamp", {{"facing","north"}})},
        {"deco_lamp_e",      BS("decotest:deco_lamp", {{"facing","east"}})},
        {"nat_bush",         BS("natmin:nat_bush", {})},
    };

    for (const auto& nb : singles) {
        GlobalPalette p2;
        p2.intern({"minecraft:air", {}});
        mesh::VoxelGrid g2;
        g2.box = {0, 0, 0, 0, 2, 0};
        g2.v.assign(3, 0);
        g2.v[1] = p2.intern(nb.bs);

        g2.v[0] = p2.intern({"minecraft:stone", {}});
        mesh::TextureAtlas at2;
        mods::ModelResolver m2(assets);
        auto rd2 = render::build_block_renders(p2, assets, m2, at2, 0);
        at2.bake();
        auto mm = render::build_mc_mesh(g2, rd2, at2, {});

        const int W = 256, H = 256;
        std::vector<float> zb(size_t(W) * H, -1e30f);
        std::vector<u8> im(size_t(W) * H * 4);
        for (size_t i = 0; i < im.size(); i += 4) {
            im[i] = 26; im[i + 1] = 28; im[i + 2] = 33; im[i + 3] = 255;
        }

        Cam c2; c2.w = W; c2.h = H; c2.scale = 120.0f;
        make_cam(c2, -0.75f, 0.5f, -0.9f, 0.0f, 1.0f, 0.0f);
        const int A2 = at2.width(), H2 = at2.height();
        auto proj = [&](const render::MCVertex& v, float& sx, float& sy, float& sd) {
            const float px = v.px - 0.5f, py = v.py - 0.7f, pz = v.pz - 0.5f;
            sx = px * c2.right[0] + py * c2.right[1] + pz * c2.right[2];
            sy = px * c2.up[0] + py * c2.up[1] + pz * c2.up[2];
            sd = px * c2.dir[0] + py * c2.dir[1] + pz * c2.dir[2];
            sx = W * 0.5f + sx * c2.scale;
            sy = H * 0.5f - sy * c2.scale;
        };
        auto tri = [&](const render::MCVertex& a, const render::MCVertex& b,
                       const render::MCVertex& cc) {
            float x[3], y[3], d[3];
            proj(a, x[0], y[0], d[0]); proj(b, x[1], y[1], d[1]); proj(cc, x[2], y[2], d[2]);
            const float minx = std::max(0.0f, std::floor(std::min({x[0], x[1], x[2]})));
            const float maxx = std::min(float(W - 1), std::ceil(std::max({x[0], x[1], x[2]})));
            const float miny = std::max(0.0f, std::floor(std::min({y[0], y[1], y[2]})));
            const float maxy = std::min(float(H - 1), std::ceil(std::max({y[0], y[1], y[2]})));
            auto edge = [](float x0, float y0, float x1, float y1, float px, float py) {
                return (px - x0) * (y1 - y0) - (py - y0) * (x1 - x0);
            };
            const float area = edge(x[0], y[0], x[1], y[1], x[2], y[2]);
            if (std::fabs(area) < 1e-9f) return;
            for (int py2 = int(miny); py2 <= int(maxy); ++py2)
                for (int px2 = int(minx); px2 <= int(maxx); ++px2) {
                    const float w0 = edge(x[1], y[1], x[2], y[2], px2 + 0.5f, py2 + 0.5f) / area;
                    const float w1 = edge(x[2], y[2], x[0], y[0], px2 + 0.5f, py2 + 0.5f) / area;
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                    const float dd = w0 * d[0] + w1 * d[1] + w2 * d[2];
                    const size_t pi = size_t(py2) * W + px2;
                    if (dd <= zb[pi] + 1e-5f) continue;
                    const float u = w0 * a.u + w1 * b.u + w2 * cc.u;
                    const float v = w0 * a.v + w1 * b.v + w2 * cc.v;
                    const int tx = std::clamp(int(u * A2), 0, A2 - 1);
                    const int ty = std::clamp(int(v * H2), 0, H2 - 1);
                    const u8* tex = &at2.pixels()[(size_t(ty) * A2 + tx) * 4];
                    if (tex[3] < 128) continue;
                    const float cr = w0 * a.r + w1 * b.r + w2 * cc.r;
                    const float cg = w0 * a.g + w1 * b.g + w2 * cc.g;
                    const float cb = w0 * a.b + w1 * b.b + w2 * cc.b;
                    zb[pi] = dd;
                    im[pi * 4 + 0] = u8(std::min(255.0f, tex[0] * cr));
                    im[pi * 4 + 1] = u8(std::min(255.0f, tex[1] * cg));
                    im[pi * 4 + 2] = u8(std::min(255.0f, tex[2] * cb));
                    im[pi * 4 + 3] = 255;
                }
        };
        for (size_t i = 0; i + 2 < mm.indices_alpha.size(); i += 3)
            tri(mm.vertices[mm.indices_alpha[i]], mm.vertices[mm.indices_alpha[i + 1]], mm.vertices[mm.indices_alpha[i + 2]]);
        for (size_t i = 0; i + 2 < mm.indices.size(); i += 3)
            tri(mm.vertices[mm.indices[i]], mm.vertices[mm.indices[i + 1]], mm.vertices[mm.indices[i + 2]]);

        auto png = exporter::encode_png(im.data(), W, H);
        fs::path p = outdir / ("block_" + std::string(nb.name) + ".png");
        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    }
    std::printf("wrote %zu per-block images to %s\n", singles.size(), outdir.c_str());

    {
        const auto* rm = models.resolve_state(BS("decotest:deco_lamp", {{"facing","north"}}));
        CHECK(rm != nullptr);
        if (rm) {
            CHECK(rm->boxes.empty());

            CHECK(rm->quads.size() == 18);
            CHECK(rm->var_ry == 0);
            CHECK(rm->quads[0].texture == "decotest:block/deco_lamp");

            bool base_top = false;
            for (const auto& q : rm->quads) {
                bool all_y4 = true, in_4_12 = true;
                for (int i = 0; i < 4; ++i) {
                    all_y4 &= std::fabs(q.p[i][1] - 4.0f) < 0.01f;
                    for (int a = 0; a < 3; a += 2)
                        in_4_12 &= (std::fabs(q.p[i][a] - 4.0f) < 0.01f ||
                                    std::fabs(q.p[i][a] - 12.0f) < 0.01f);
                }
                if (all_y4 && in_4_12) base_top = true;
            }
            CHECK(base_top);

            bool irrational_coord = false;
            for (const auto& q : rm->quads)
                for (int i = 0; i < 4 && !irrational_coord; ++i)
                    for (int a = 0; a < 3; ++a) {
                        const f32 r = std::fabs(q.p[i][a] - std::round(q.p[i][a]));
                        if (r > 0.01f && r < 0.99f) irrational_coord = true;
                    }
            CHECK(irrational_coord);
        }

        const auto* rm_e = models.resolve_state(BS("decotest:deco_lamp", {{"facing","east"}}));
        CHECK(rm_e && rm_e->var_ry == 90);

        const auto* rn = models.resolve_state(BS("natmin:nat_bush", {}));
        CHECK(rn != nullptr);
        if (rn) {
            CHECK(rn->quads.size() == 6);
            CHECK(rn->quads[2].texture == "natmin:block/nat_bush");

            bool half_top = false;
            for (const auto& q : rn->quads) {
                bool all_y8 = true;
                for (int i = 0; i < 4; ++i) all_y8 &= std::fabs(q.p[i][1] - 8.0f) < 0.01f;
                if (all_y8) half_top = true;
            }
            CHECK(half_top);
        }

        CHECK(assets.resolve_block_texture("decotest:deco_lamp") ==
              std::optional<std::string>("decotest:block/deco_lamp"));
        CHECK(assets.resolve_block_texture("natmin:nat_bush") ==
              std::optional<std::string>("natmin:block/nat_bush"));

        auto strip = assets.load_texture("decotest:block/deco_strip");
        auto tall = assets.load_texture("decotest:block/deco_tall");
        CHECK(strip && strip->animated);
        CHECK(tall && !tall->animated);

        {
            mods::TextureRGBA t;
            t.w = t.h = 4;
            t.pixels.assign(16 * 4, 0);
            t.pixels[0] = 255; t.pixels[3] = 255;
            mesh::TextureAtlas ab;
            const int ti = ab.add("bleed_test", t);
            ab.bake();
            int bled = 0;
            for (size_t i = 0; i + 3 < ab.pixels().size(); i += 4)
                if (ab.pixels()[i] == 255 && ab.pixels()[i + 3] == 0) ++bled;
            CHECK(ti >= 0 && bled > 0);
        }
    }

    {
        GlobalPalette bp;
        const u16 id_bar = bp.intern({"minecraft:barrier", {}});
        const u16 id_st  = bp.intern({"minecraft:stone", {}});

        mods::AssetRegistry bare;
        mods::ModelResolver bmodels(bare);
        mesh::TextureAtlas atb;
        auto brs = render::build_block_renders(bp, bare, bmodels, atb, 0);
        atb.bake();

        CHECK(brs[id_bar].barrier);
        CHECK(!brs[id_bar].full_opaque_cube);
        CHECK(brs[id_bar].boxes.size() == 1);
        int btile = -1, bpresent = 0;
        for (int f = 0; f < 6; ++f)
            if (brs[id_bar].boxes[0].faces[f].tile >= 0) {
                btile = brs[id_bar].boxes[0].faces[f].tile;
                ++bpresent;
            }
        CHECK(bpresent == 6);
        CHECK(btile >= 0 && atb.tile_name(btile) == "dine:barrier");

        auto avg = bare.average_color("minecraft:barrier");
        CHECK(avg.has_value());
        if (avg) CHECK((*avg)[0] > 150 && (*avg)[1] < 100 && (*avg)[2] < 100);
        auto mc = render::top_map_color({"minecraft:barrier", {}}, nullptr);
        CHECK(mc[0] > 150 && mc[1] < 100 && mc[2] < 100);

        mesh::VoxelGrid g;
        g.box = {0, 0, 0, 1, 0, 0};
        g.v = {id_st, id_bar};
        render::MCMeshOptions on, off;
        off.skip_barriers = true;
        auto m1 = render::build_mc_mesh(g, brs, atb, on, nullptr);
        auto m2 = render::build_mc_mesh(g, brs, atb, off, nullptr);
        CHECK(m1.quads_by_block[id_bar] > 0);
        CHECK(m1.quads_by_block[id_st] > 0);
        CHECK(m2.quads_by_block[id_bar] == 0);
        CHECK(m2.quads_by_block[id_st] > 0);
    }

    // 7) Жидкости: высота по level, OBJ квадами.
    {
        GlobalPalette fp;
        const u16 wid0 = fp.intern(BS("minecraft:water", {{"level", "0"}}));
        const u16 wid3 = fp.intern(BS("minecraft:water", {{"level", "3"}}));
        mods::AssetRegistry fa;
        mods::ModelResolver fmodels(fa);
        mesh::TextureAtlas fat;
        auto fr = render::build_block_renders(fp, fa, fmodels, fat, 0);
        fat.bake();
        CHECK(!fr[wid0].boxes.empty());
        CHECK(!fr[wid3].boxes.empty());
        if (!fr[wid0].boxes.empty())
            CHECK(std::fabs(fr[wid0].boxes[0].to[1] - 16.0f) < 0.01f);
        if (!fr[wid3].boxes.empty())
            CHECK(std::fabs(fr[wid3].boxes[0].to[1] - 16.0f * 5.0f / 8.0f) < 0.01f);
        CHECK(fr[wid0].fluid && !fr[wid0].full_opaque_cube);

        mesh::VoxelGrid wg;
        wg.box = {0, 0, 0, 1, 0, 0};
        wg.v = {wid0, wid0};
        render::MCMeshOptions wo;
        auto wm = render::build_mc_mesh(wg, fr, fat, wo, nullptr);
        CHECK(wm.indices.empty());
        CHECK(wm.indices_alpha.size() / 6 == 10);   // 2*6 минус общая грань

        mesh::VoxelGrid wv;
        wv.box = {0, 0, 0, 0, 1, 0};
        wv.v = {wid0, wid0};
        auto w2 = render::build_mc_mesh(wv, fr, fat, wo, nullptr);
        CHECK(w2.indices_alpha.size() / 6 == 10);   // стык верх/низ скрыт

        mesh::VoxelGrid wl;
        wl.box = {0, 0, 0, 1, 0, 0};
        wl.v = {wid0, wid3};
        auto w3 = render::build_mc_mesh(wl, fr, fat, wo, nullptr);
        CHECK(w3.indices_alpha.size() / 6 == 11);   // общий борт — только у высокого
    }

    {
        render::MCMesh dm;
        const f32 P[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
        for (auto& p : P)
            dm.vertices.push_back({p[0], p[1], p[2], 0.1f, 0.9f, 1, 1, 1, 0, 0, 1});
        for (int rpt = 0; rpt < 2; ++rpt)
            dm.indices.insert(dm.indices.end(), {0, 1, 2, 0, 2, 3});
        mesh::TextureAtlas dat;
        mods::TextureRGBA t;
        t.w = t.h = 4;
        t.pixels.assign(static_cast<size_t>(4 * 4 * 4), 255);
        dat.add("test:solid", t);
        dat.bake();
        exporter::MCExportContext dctx{&dm, &dat, 1.0f, nullptr};
        const fs::path dd = outdir / "dedup_check";
        fs::create_directories(dd);
        CHECK(exporter::export_mc_obj(dctx, dd / "world.obj"));
        CHECK(fs::exists(dd / "world_mod_solid.png"));
        std::ifstream dmtl(dd / "world.mtl");
        std::string dmtl_text((std::istreambuf_iterator<char>(dmtl)), {});
        CHECK(dmtl_text.find("newmtl mod_solid") != std::string::npos);
        CHECK(dmtl_text.find("map_Kd world_mod_solid.png") != std::string::npos);
        std::ifstream din(dd / "world.obj");
        size_t flines = 0, vlines = 0;
        std::string ln;
        while (std::getline(din, ln)) {
            if (ln.starts_with("f ")) ++flines;
            if (ln.starts_with("v ")) ++vlines;
        }
        CHECK(flines == 1);   // дубликат квада отрезан (z-fight)
        CHECK(vlines == 4);   // вершины сварены
    }

    {
        const fs::path qdir = outdir / "quad_check";
        fs::create_directories(qdir);
        exporter::MCExportContext xctx;
        xctx.mesh = &mesh;
        xctx.atlas = &atlas;
        xctx.renders = &renders;
        CHECK(exporter::export_mc_obj(xctx, qdir / "world.obj"));
        std::ifstream in(qdir / "world.obj");
        CHECK(in.good());
        std::vector<std::array<f32, 3>> vs, ns;
        size_t f4 = 0, fother = 0, bad_orient = 0;
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream ss(line);
            std::string tag;
            ss >> tag;
            if (tag == "v") {
                std::array<f32, 3> p{};
                ss >> p[0] >> p[1] >> p[2];
                vs.push_back(p);
            } else if (tag == "vn") {
                std::array<f32, 3> n{};
                ss >> n[0] >> n[1] >> n[2];
                ns.push_back(n);
            } else if (tag == "f") {
                std::vector<u32> ids;
                std::string tok;
                while (ss >> tok) ids.push_back(static_cast<u32>(std::stoul(tok)) - 1);
                if (ids.size() == 4) ++f4; else ++fother;
                if (ids.size() >= 3 && ids[0] < vs.size()) {
                    const auto& A = vs[ids[0]];
                    f32 U[3]{}, V[3]{};
                    for (int k = 0; k < 3; ++k) {
                        U[k] = vs[ids[1]][k] - A[k];
                        V[k] = vs[ids[2]][k] - A[k];
                    }
                    const f32 dot = (U[1] * V[2] - U[2] * V[1]) * ns[ids[0]][0] +
                                    (U[2] * V[0] - U[0] * V[2]) * ns[ids[0]][1] +
                                    (U[0] * V[1] - U[1] * V[0]) * ns[ids[0]][2];
                    if (dot < 0) ++bad_orient;
                }
            }
        }
        const size_t want = (mesh.indices.size() + mesh.indices_alpha.size()) / 6;
        CHECK(f4 <= want && fother == 0);
        CHECK(bad_orient == 0);
    }

    // 8) Waterlogged: вода внутри ячейки; skip_fluids убирает её.
    {
        GlobalPalette wp;
        const u16 fid_wet = wp.intern(BS("minecraft:oak_fence", {
            {"east","false"},{"north","false"},{"south","false"},
            {"waterlogged","true"},{"west","false"}}));
        const u16 fid_dry = wp.intern(BS("minecraft:oak_fence", {
            {"east","false"},{"north","false"},{"south","false"},
            {"waterlogged","false"},{"west","false"}}));
        const u16 wid = wp.intern(BS("minecraft:water", {{"level","0"}}));
        mesh::TextureAtlas wat_atlas;
        auto wr = render::build_block_renders(wp, assets, models, wat_atlas, 0);
        wat_atlas.bake();
        CHECK(wr[fid_wet].waterlogged);
        CHECK(!wr[fid_dry].waterlogged);
        CHECK(!wr[fid_wet].full_opaque_cube);

        render::MCMeshOptions wo;
        mesh::VoxelGrid g_wet;
        g_wet.box = {0, 0, 0, 0, 0, 0};
        g_wet.v = {fid_wet};
        auto m_wet = render::build_mc_mesh(g_wet, wr, wat_atlas, wo, nullptr);
        mesh::VoxelGrid g_dry;
        g_dry.box = {0, 0, 0, 0, 0, 0};
        g_dry.v = {fid_dry};
        auto m_dry = render::build_mc_mesh(g_dry, wr, wat_atlas, wo, nullptr);
        CHECK(m_dry.indices_alpha.empty());
        CHECK(m_wet.indices_alpha.size() / 6 == 6);   // 6 граней воды вокруг столбика

        mesh::VoxelGrid g_adj;
        g_adj.box = {0, 0, 0, 1, 0, 0};
        g_adj.v = {fid_wet, wid};
        auto m_adj = render::build_mc_mesh(g_adj, wr, wat_atlas, wo, nullptr);
        CHECK(m_adj.indices_alpha.size() / 6 == 10);  // 5+5: общий борт скрыт с двух сторон

        render::MCMeshOptions sf;
        sf.skip_fluids = true;
        auto m_skip = render::build_mc_mesh(g_adj, wr, wat_atlas, sf, nullptr);
        CHECK(m_skip.indices_alpha.empty());
    }

    // 9) Биом-тон по позиции из biome_pal.
    {
        GlobalPalette bp;
        const u16 gid = bp.intern(BS("minecraft:grass_block", {{"snowy","false"}}));
        mesh::TextureAtlas bat;
        auto br = render::build_block_renders(bp, assets, models, bat, 0);
        bat.bake();
        CHECK(br[gid].tint_family == 1);
        mesh::VoxelGrid bg;
        bg.box = {0, 0, 0, 0, 0, 0};
        bg.v = {gid};
        bg.biom = {42};
        bg.biome_pal.assign(43, {});
        bg.biome_pal[42].grass = {10, 200, 30};
        auto bm = render::build_mc_mesh(bg, br, bat, render::MCMeshOptions{}, nullptr);
        bool found_up = false, ok_rgb = true;
        for (const auto& v : bm.vertices)
            if (v.ny > 0.5f) {
                found_up = true;
                ok_rgb = ok_rgb && std::fabs(v.r - 10.0f / 255.0f) < 0.002f &&
                         std::fabs(v.g - 200.0f / 255.0f) < 0.002f &&
                         std::fabs(v.b - 30.0f / 255.0f) < 0.002f;
            }
        CHECK(found_up && ok_rgb);
    }

    // 10) Экспорт сохраняет blend-проход даже при непрозрачном PNG.
    {
        render::MCMesh am;
        am.vertices = {
            {0,0,0, 0,0, 1,1,1, 0,0,1}, {1,0,0, 1,0, 1,1,1, 0,0,1},
            {1,1,0, 1,1, 1,1,1, 0,0,1}, {0,1,0, 0,1, 1,1,1, 0,0,1},
        };
        am.indices_alpha = {0, 1, 2, 0, 2, 3};
        mesh::TextureAtlas aa;
        mods::TextureRGBA opaque;
        opaque.w = opaque.h = 16;
        opaque.pixels.assign(16 * 16 * 4, 255); // намеренно непрозрачный PNG
        aa.add("test:blend_from_render_pass", opaque);
        aa.bake();
        const fs::path ap = outdir / "blend_pass.glb";
        CHECK(exporter::export_mc_glb({&am, &aa, 1.0f, nullptr}, ap));
        std::ifstream af(ap, std::ios::binary);
        std::vector<char> ab((std::istreambuf_iterator<char>(af)), {});
        u32 json_len = 0;
        if (ab.size() >= 20) std::memcpy(&json_len, ab.data() + 12, sizeof(json_len));
        std::string aj = ab.size() >= 20 + json_len ? std::string(ab.data() + 20, json_len) : "";
        CHECK(aj.find("\"alphaMode\":\"BLEND\"") != std::string::npos);
        CHECK(aj.find("\"doubleSided\":true") != std::string::npos);
    }

    // 11) GLB: контейнер glTF 2.0, COLOR_0.
    {
        const fs::path gdir = outdir / "glb_check";
        fs::create_directories(gdir);
        exporter::MCExportContext gctx;
        gctx.mesh = &mesh;
        gctx.atlas = &atlas;
        gctx.renders = &renders;
        CHECK(exporter::export_mc_glb(gctx, gdir / "world.glb"));
        const auto fsize = fs::file_size(gdir / "world.glb");
        std::ifstream gf(gdir / "world.glb", std::ios::binary);
        CHECK(gf.good());
        std::vector<u8> bytes(fsize);
        gf.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(fsize));
        auto rd32 = [&](size_t o) {
            u32 x = 0;
            std::memcpy(&x, bytes.data() + o, 4);
            return x;
        };
        CHECK(fsize > 20 && std::memcmp(bytes.data(), "glTF", 4) == 0);
        CHECK(rd32(4) == 2);
        CHECK(rd32(8) == fsize);
        const u32 jlen = rd32(12);
        CHECK(rd32(16) == 0x4E4F534A);   // 'JSON'
        const std::string js(reinterpret_cast<const char*>(bytes.data() + 20), jlen);
        CHECK(js.find("\"asset\"") != std::string::npos);
        CHECK(js.find("COLOR_0") != std::string::npos);
        CHECK(js.find("\"image/png\"") != std::string::npos);
        size_t tri_indices = 0;
        const std::string needle = "\"componentType\":5125,\"count\":";
        for (size_t p = js.find(needle); p != std::string::npos;
             p = js.find(needle, p + needle.size()))
            tri_indices += std::strtoul(js.c_str() + p + needle.size(), nullptr, 10);
        exporter::MCBaked bk;
        exporter::MCExportContext bctx{gctx.mesh, gctx.atlas, gctx.scale, nullptr};
        CHECK(exporter::bake_mc_export(bctx, bk));
        CHECK(tri_indices == bk.quads.size() * 6);
        const fs::path chunk_dir = gdir / "chunks";
        const size_t nchunks = exporter::export_mc_glb_chunks(gctx, chunk_dir, "world", 4);
        CHECK(nchunks > 1);
        CHECK(fs::exists(chunk_dir / "world_x0_z0.glb"));
    }

    // 9) Листва: cutout-крона. Листва — «полный куб» геометрически, но не
    //    окклюдер: внутренние грани обязаны пережить генерацию меша, иначе
    //    никакой doubleSided их потом не вернёт.
    {
        GlobalPalette lp;
        const u16 id_leaf = lp.intern({"minecraft:oak_leaves", {}});
        const u16 id_stone = lp.intern({"minecraft:stone", {}});
        mods::AssetRegistry la;
        mods::ModelResolver lm(la);
        mesh::TextureAtlas lat;
        auto lr = render::build_block_renders(lp, la, lm, lat, 0);
        lat.bake();
        CHECK(lr[id_leaf].foliage);
        CHECK(!lr[id_stone].foliage);

        // Две листвы рядом: в «густом» режиме общие грани остаются.
        mesh::VoxelGrid lg;
        lg.box = {0, 0, 0, 1, 0, 0};
        lg.v = {id_leaf, id_leaf};
        render::MCMeshOptions fancy, fast;
        fast.fancy_leaves = false;
        auto mf = render::build_mc_mesh(lg, lr, lat, fancy, nullptr);
        auto ms = render::build_mc_mesh(lg, lr, lat, fast, nullptr);
        CHECK(mf.indices.size() / 6 == 12);   // 2 блока x 6 граней
        CHECK(ms.indices.size() / 6 == 10);   // общая грань удалена

        // Камень рядом с листвой: его грань к листве не должна пропадать,
        // иначе сквозь cutout-текстуру видна дыра.
        mesh::VoxelGrid sg;
        sg.box = {0, 0, 0, 1, 0, 0};
        sg.v = {id_stone, id_leaf};
        auto sf = render::build_mc_mesh(sg, lr, lat, fancy, nullptr);
        auto ss = render::build_mc_mesh(sg, lr, lat, fast, nullptr);
        // fancy: камень сохраняет грань к листве (6), листва прячет свою
        // грань к камню-окклюдеру (5) => 11 квадов.
        CHECK(sf.indices.size() / 6 == 11);
        // fast: обе стыковые грани удалены => 10.
        CHECK(ss.indices.size() / 6 == 10);
    }

    std::printf("\n%s (%d failures)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
