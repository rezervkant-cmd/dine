#include "mc_mesher.hpp"
#include "core/block_colors.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace vw::render {

using mods::FACE_WEST;
using mods::FACE_EAST;
using mods::FACE_DOWN;
using mods::FACE_UP;
using mods::FACE_NORTH;
using mods::FACE_SOUTH;

namespace {

constexpr f32 kFaceShade[6] = {
    0.6f,
    0.6f,
    0.5f,
    1.0f,
    0.8f,
    0.8f,
};

constexpr int kDX[6] = {-1, 1, 0, 0, 0, 0};
constexpr int kDY[6] = {0, 0, -1, 1, 0, 0};
constexpr int kDZ[6] = {0, 0, 0, 0, -1, 1};

constexpr int kFaceAxis[6] = {0, 0, 1, 1, 2, 2};

constexpr f32 kFaceNormal[6][3] = {
    {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};

int norm_deg(f32 d) {
    int r = static_cast<int>(std::lround(d)) % 360;
    if (r < 0) r += 360;
    return r;
}
bool is_mult90(f32 d) {
    return std::fabs(d - std::round(d / 90.0f) * 90.0f) < 0.001f;
}

int rot_dir_x_step(int d, bool& flip) {
    switch (d) {
        case FACE_DOWN:  return FACE_SOUTH;
        case FACE_SOUTH: return FACE_UP;
        case FACE_UP:    flip = true; return FACE_NORTH;
        case FACE_NORTH: flip = true; return FACE_DOWN;
        default: return d;
    }
}

int rot_dir_y_step(int d) {
    switch (d) {
        case FACE_NORTH: return FACE_EAST;
        case FACE_EAST:  return FACE_SOUTH;
        case FACE_SOUTH: return FACE_WEST;
        case FACE_WEST:  return FACE_NORTH;
        default: return d;
    }
}

struct QuadPt { f32 x, y, z, u, v; };

inline void rot_x90(f32& y, f32& z) {
    const f32 ny = (z - 8.0f) + 8.0f;
    const f32 nz = 8.0f - (y - 8.0f);
    y = ny; z = nz;
}

inline void rot_y90(f32& x, f32& z) {
    const f32 nx = 8.0f - (z - 8.0f);
    const f32 nz = (x - 8.0f) + 8.0f;
    x = nx; z = nz;
}

inline void rot_uv(f32& u, f32& v, f32 deg) {
    const f32 c = std::cos(deg * 0.017453292519943295f);
    const f32 s = std::sin(deg * 0.017453292519943295f);
    const f32 nu = (u - 8.0f) * c - (v - 8.0f) * s + 8.0f;
    const f32 nv = (u - 8.0f) * s + (v - 8.0f) * c + 8.0f;
    u = nu; v = nv;
}

std::optional<std::array<f32, 3>> biome_tint_rgb(std::string_view n) {
    auto has = [&](std::string_view s) { return n.find(s) != std::string_view::npos; };
    if (has("grass_block") || n == "minecraft:grass" || n == "minecraft:short_grass" ||
        has("tall_grass") || has("fern") || has("sugar_cane"))
        return std::array<f32, 3>{0.57f, 0.74f, 0.35f};
    if (has("leaves") || has("vine"))
        return std::array<f32, 3>{0.47f, 0.67f, 0.18f};
    if (has("water")) return std::array<f32, 3>{0.25f, 0.46f, 0.89f};
    return std::nullopt;
}

bool alpha_blend_name(std::string_view n) {
    return n.find("water") != std::string_view::npos ||
           (n.find("glass") != std::string_view::npos &&
            n.find("glass_pane") == std::string_view::npos) ||
           n.find("ice") != std::string_view::npos;
}

mods::TextureRGBA make_barrier_texture() {
    mods::TextureRGBA t;
    t.w = t.h = 16;
    t.pixels.assign(static_cast<size_t>(16 * 16 * 4), 0);
    t.source = "dine:barrier";
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const f32 dx = static_cast<f32>(x) + 0.5f - 8.0f;
            const f32 dy = static_cast<f32>(y) + 0.5f - 8.0f;
            const f32 r = std::sqrt(dx * dx + dy * dy);
            const bool ring = r > 4.4f && r < 6.4f;
            const bool slash = std::fabs(dx - dy) < 1.2f && r < 6.0f;
            if (!ring && !slash) continue;
            const size_t i = (static_cast<size_t>(y) * 16 + static_cast<size_t>(x)) * 4;
            t.pixels[i + 0] = 219;
            t.pixels[i + 1] = 33;
            t.pixels[i + 2] = 26;
            t.pixels[i + 3] = 255;
        }
    return t;
}

}

std::vector<BlockRender> build_block_renders(const GlobalPalette& palette,
                                             mods::AssetRegistry& assets,
                                             mods::ModelResolver& models,
                                             mesh::TextureAtlas& atlas,
                                             int texture_mode) {
    auto pal_snap = palette.snapshot();
    std::vector<BlockRender> out(pal_snap.size());

    auto tile_for = [&](const std::string& tex_ref, const std::string& block) -> int {
        if (texture_mode == 0 && tex_ref == "dine:barrier") {

            if (atlas.has(tex_ref)) return atlas.index_of(tex_ref);
            return atlas.add(tex_ref, make_barrier_texture());
        }
        if (texture_mode == 0 && !tex_ref.empty()) {
            if (atlas.has(tex_ref)) return atlas.index_of(tex_ref);
            if (auto t = assets.load_texture(tex_ref)) return atlas.add(tex_ref, *t);
        }
        if (texture_mode <= 1)
            if (auto c = assets.average_color(block))
                return atlas.add_solid("avg:" + block, *c);
        return atlas.add_solid("flat:" + block, fallback_block_color(block));
    };

    for (size_t i = 0; i < pal_snap.size(); ++i) {
        const auto& bs = pal_snap[i];
        BlockRender& br = out[i];
        if (bs.is_air()) continue;

        br.fluid = bs.is_fluid();
        if (br.fluid)
            br.fluid_kind = bs.name.find("lava") != std::string::npos ? 2 : 1;

        const std::string_view local_name(
            bs.name.c_str() + (bs.name.find(':') == std::string::npos ? 0 : bs.name.find(':') + 1));
        br.bedrock = (local_name == "bedrock");
        br.foliage = local_name == "leaves" ||
                     local_name.find("_leaves") != std::string_view::npos;
        // Водозаполненные ячейки: внутри тоже вода.
        {
            bool wl = false;
            for (auto& [k, v] : bs.props)
                if (k == "waterlogged" && v == "true") wl = true;
            if (local_name.find("seagrass") != std::string_view::npos ||
                local_name.find("kelp") != std::string_view::npos)
                wl = true;
            br.waterlogged = wl;
        }
        br.alpha_blend = alpha_blend_name(bs.name);
        if (auto t = biome_tint_rgb(bs.name)) {
            br.tint[0] = (*t)[0];
            br.tint[1] = (*t)[1];
            br.tint[2] = (*t)[2];
            auto hasn = [&](std::string_view s) { return bs.name.find(s) != std::string::npos; };
            if (hasn("grass_block") || bs.name == "minecraft:grass" ||
                bs.name == "minecraft:short_grass" || hasn("tall_grass") ||
                hasn("fern") || hasn("sugar_cane"))
                br.tint_family = 1;
            else if (hasn("leaves") || hasn("vine"))
                br.tint_family = 2;
            else if (hasn("water"))
                br.tint_family = 3;
        }

        const mods::ResolvedModel* rm =
            texture_mode == 0 ? models.resolve_state(bs) : nullptr;

        if (rm && rm->cross) {
            br.cross = true;
            br.decor = true;
            br.cross_tile = tile_for(rm->cross_texture, bs.name);
            continue;
        }

        auto add_box = [&](const mods::ModelBox& mb) {
            BlockRender::Box box{};
            std::memcpy(box.from, mb.from, sizeof(box.from));
            std::memcpy(box.to, mb.to, sizeof(box.to));
            box.has_rot = mb.has_rot;
            box.rot_axis = mb.rot_axis;
            box.rot_angle = mb.rot_angle;
            std::memcpy(box.rot_origin, mb.rot_origin, sizeof(box.rot_origin));
            box.rot_rescale = mb.rot_rescale;
            box.var_rx = mb.var_rx;
            box.var_ry = mb.var_ry;
            box.var_uvlock = mb.var_uvlock;
            for (int f = 0; f < 6; ++f) {
                const auto& src = mb.faces[f];
                if (!src.present) continue;
                auto& dst = box.faces[f];
                dst.tile = tile_for(src.texture, bs.name);
                dst.u0 = src.u0; dst.v0 = src.v0; dst.u1 = src.u1; dst.v1 = src.v1;
                dst.uv_rot = src.uv_rot;
                dst.cull = src.cullface;
                dst.shade = src.shade;
                dst.tint = src.tint ||
                           (br.tint[0] != 1 || br.tint[1] != 1 || br.tint[2] != 1);
            }
            br.boxes.push_back(box);
        };

        if (local_name == "barrier") {
            br.barrier = true;
            mods::ModelBox mb;
            for (int f = 0; f < 6; ++f) {
                mb.faces[f].present = true;
                mb.faces[f].texture = "dine:barrier";
                mb.faces[f].cullface = true;
            }
            add_box(mb);
            continue;
        }

        if (rm && (!rm->boxes.empty() || !rm->quads.empty())) {
            for (const auto& mb : rm->boxes) add_box(mb);
            if (!rm->boxes.empty() && rm->quads.empty())
                br.full_opaque_cube = rm->full_cube && !br.alpha_blend;

            if (bs.name == "minecraft:grass_block")
                for (auto& b : br.boxes)
                    for (int f = 0; f < 6; ++f)
                        b.faces[f].tint = (f == FACE_UP);

            if (!rm->full_cube && rm->boxes.size() <= 2) {
                f32 vol = 0;
                for (auto& b : rm->boxes)
                    vol += (b.to[0]-b.from[0]) * (b.to[1]-b.from[1]) * (b.to[2]-b.from[2]);
                br.decor = vol < 16 * 16 * 4;
            }

            br.var_rx = rm->var_rx;
            br.var_ry = rm->var_ry;
            for (const auto& bq : rm->quads) {
                BlockRender::Quad q{};
                std::memcpy(q.p, bq.p, sizeof(q.p));
                std::memcpy(q.u, bq.u, sizeof(q.u));
                std::memcpy(q.v, bq.v, sizeof(q.v));
                q.shade = bq.shade;
                q.tile = tile_for(bq.texture, bs.name);
                br.quads.push_back(q);
            }
            if (!rm->quads.empty() && rm->boxes.empty()) br.decor = true;
        } else {

            mods::ModelBox mb;
            std::string tex;
            if (texture_mode == 0)
                if (auto ref = assets.resolve_block_texture(bs.name)) tex = *ref;
            for (int f = 0; f < 6; ++f) {
                mb.faces[f].present = true;
                mb.faces[f].texture = tex;
                mb.faces[f].cullface = true;
            }
            add_box(mb);
            br.full_opaque_cube = !br.alpha_blend;
        }
        // Жидкости: высота поверхности по level.
        if (br.fluid && !br.boxes.empty()) {
            int level = 0;
            for (auto& [k, v] : bs.props)
                if (k == "level") { level = std::atoi(v.c_str()); break; }
            f32 h = 16.0f;
            if (level > 0 && level < 8)
                h = 16.0f * (8.0f - static_cast<f32>(level)) / 8.0f;
            if (h < 2.0f) h = 2.0f;
            for (auto& b : br.boxes) b.to[1] = h;
            br.fluid_top = h;
        }
        if (br.waterlogged && br.full_opaque_cube) br.waterlogged = false;
    }
    return out;
}

MCMesh build_mc_mesh(const mesh::VoxelGrid& grid,
                     const std::vector<BlockRender>& renders,
                     const mesh::TextureAtlas& atlas,
                     const MCMeshOptions& opt,
                     ThreadPool* pool) {
    const BBox& bb = grid.box;

    auto rendered = [&](u16 id) {
        const auto& r = renders[id];
        if (r.empty()) return false;
        if (opt.skip_fluids && r.fluid) return false;
        if (opt.skip_decor && r.decor) return false;
        if (opt.skip_bedrock && r.bedrock) return false;
        if (opt.skip_barriers && r.barrier) return false;
        return true;
    };
    auto occludes = [&](u16 id) {
        if (!rendered(id)) return false;
        const auto& r = renders[id];
        // Листва в «густом» режиме — cutout-материал: она не перекрывает
        // ни соседние блоки, ни соседнюю листву, иначе внутренние грани
        // кроны не создаются вовсе (doubleSided их уже не вернёт).
        if (opt.fancy_leaves && r.foliage) return false;
        return r.full_opaque_cube;
    };

    // Рендер воды для водозаполненных ячеек (самая высокая поверхность).
    const BlockRender* water_br = nullptr;
    for (const auto& r : renders)
        if (r.fluid && r.alpha_blend && !r.boxes.empty())
            if (!water_br || r.fluid_top > water_br->fluid_top) water_br = &r;

    const i32 y_lo = bb.y0, y_hi = bb.y1;
    const int nparts = pool ? static_cast<int>(std::min<u32>(pool->size(), 8)) : 1;
    std::vector<MCMesh> parts(static_cast<size_t>(nparts));

    auto mesh_layer_range = [&](int part, i32 py0, i32 py1) {
        MCMesh& out = parts[static_cast<size_t>(part)];
        out.quads_by_block.assign(renders.size(), 0);

        auto build_quad = [&](const BlockRender::Box& box, int f, std::array<QuadPt, 4>& q,
                              int& eff_dir, bool& axis_aligned) -> const BlockRender::FaceTex* {
            const auto& ft = box.faces[f];
            if (ft.tile < 0) return nullptr;
            const f32 x0 = box.from[0], y0 = box.from[1], z0 = box.from[2];
            const f32 x1 = box.to[0], y1 = box.to[1], z1 = box.to[2];
            const f32 u0 = ft.u0, v0 = ft.v0, u1 = ft.u1, v1 = ft.v1;

            switch (f) {
                case FACE_WEST:  q = {{{x0,y0,z1,u0,v1},{x0,y0,z0,u1,v1},{x0,y1,z0,u1,v0},{x0,y1,z1,u0,v0}}}; break;
                case FACE_EAST:  q = {{{x1,y0,z0,u0,v1},{x1,y0,z1,u1,v1},{x1,y1,z1,u1,v0},{x1,y1,z0,u0,v0}}}; break;
                case FACE_DOWN:  q = {{{x0,y0,z0,u0,v0},{x1,y0,z0,u1,v0},{x1,y0,z1,u1,v1},{x0,y0,z1,u0,v1}}}; break;
                case FACE_UP:    q = {{{x0,y1,z1,u0,v1},{x1,y1,z1,u1,v1},{x1,y1,z0,u1,v0},{x0,y1,z0,u0,v0}}}; break;
                case FACE_NORTH: q = {{{x1,y0,z0,u0,v1},{x0,y0,z0,u1,v1},{x0,y1,z0,u1,v0},{x1,y1,z0,u0,v0}}}; break;
                default:         q = {{{x0,y0,z1,u0,v1},{x1,y0,z1,u1,v1},{x1,y1,z1,u1,v0},{x0,y1,z1,u0,v0}}}; break;
            }

            if (ft.uv_rot) {
                const int k = ft.uv_rot / 90;
                std::array<QuadPt, 4> oq = q;
                for (int i = 0; i < 4; ++i) {
                    // down: поворот против часовой.
                    const int src = (i + (f == FACE_DOWN ? (4 - k) % 4 : k)) % 4;
                    const QuadPt& s = oq[src];
                    q[i].u = s.u; q[i].v = s.v;
                }
            }

            if (box.has_rot) {
                f32 angle = -box.rot_angle;
                if (box.rot_axis == 'z') angle = -angle;
                const f32 c = std::cos(angle * 0.017453292519943295f);
                const f32 s = std::sin(angle * 0.017453292519943295f);
                f32 scale = 1.0f;
                if (box.rot_rescale)
                    scale = 1.0f / std::max(std::fabs(c), std::fabs(s));
                const f32 ox = box.rot_origin[0], oy = box.rot_origin[1], oz = box.rot_origin[2];
                for (auto& p : q) {
                    if (box.rot_axis == 'x') {
                        const f32 y = p.y, z = p.z;
                        p.z = ((z - oz) * c - (y - oy) * s) * scale + oz;
                        p.y = ((z - oz) * s + (y - oy) * c) * scale + oy;
                    } else if (box.rot_axis == 'y') {
                        const f32 x = p.x, z = p.z;
                        p.x = ((x - ox) * c - (z - oz) * s) * scale + ox;
                        p.z = ((x - ox) * s + (z - oz) * c) * scale + oz;
                    } else {
                        const f32 x = p.x, y = p.y;
                        p.x = ((x - ox) * c - (y - oy) * s) * scale + ox;
                        p.y = ((x - ox) * s + (y - oy) * c) * scale + oy;
                    }
                }
            }

            eff_dir = f;
            const int rx = norm_deg(static_cast<f32>(box.var_rx));
            const int ry = norm_deg(static_cast<f32>(box.var_ry));
            if (rx) {
                for (auto& p : q)
                    for (int k = 0; k < rx / 90; ++k) rot_x90(p.y, p.z);
                if (box.var_uvlock && (eff_dir == FACE_WEST || eff_dir == FACE_EAST)) {
                    const f32 ang = eff_dir == FACE_EAST ? -static_cast<f32>(rx) : static_cast<f32>(rx);
                    for (auto& p : q) rot_uv(p.u, p.v, ang);
                }
                for (int k = 0; k < rx / 90; ++k) {
                    bool flip = false;
                    eff_dir = rot_dir_x_step(eff_dir, flip);
                    if (flip && box.var_uvlock)
                        for (auto& p : q) { p.u = 16.0f - p.u; p.v = 16.0f - p.v; }
                }
            }
            if (ry) {
                for (auto& p : q)
                    for (int k = 0; k < ry / 90; ++k) rot_y90(p.x, p.z);
                if (box.var_uvlock && (eff_dir == FACE_DOWN || eff_dir == FACE_UP)) {
                    const f32 ang = eff_dir == FACE_UP ? -static_cast<f32>(ry) : static_cast<f32>(ry);
                    for (auto& p : q) rot_uv(p.u, p.v, ang);
                }
                for (int k = 0; k < ry / 90; ++k)
                    eff_dir = rot_dir_y_step(eff_dir);
            }

            axis_aligned = (!box.has_rot || is_mult90(box.rot_angle));
            return &ft;
        };

        auto emit_face = [&](const BlockRender& br, const BlockRender::Box& box,
                             int f, i32 x, i32 y, i32 z) {
            std::array<QuadPt, 4> q;
            int eff_dir = f;
            bool axis_aligned = true;
            const BlockRender::FaceTex* ftp = build_quad(box, f, q, eff_dir, axis_aligned);
            if (!ftp) return;
            const auto& ft = *ftp;

            // Углы поверхности жидкости по соседним уровням.
            if (br.fluid && !box.has_rot) {
                auto top_at = [&](i32 sx, i32 sz) -> f32 {
                    if (sx < bb.x0 || sx > bb.x1 || sz < bb.z0 || sz > bb.z1) return -1.0f;
                    const auto& nr = renders[grid.at(sx, y, sz)];
                    if (nr.fluid && nr.fluid_kind == br.fluid_kind) return nr.fluid_top;
                    if (br.fluid_kind == 1 && nr.waterlogged) return 16.0f;
                    return -1.0f;
                };
                auto corner_top = [&](f32 px, f32 pz) -> f32 {
                    const i32 ox = px < 8.0f ? -1 : 0;
                    const i32 oz = pz < 8.0f ? -1 : 0;
                    f32 h = -1.0f;
                    for (i32 dz = oz; dz <= oz + 1; ++dz)
                        for (i32 dx = ox; dx <= ox + 1; ++dx)
                            h = std::max(h, top_at(x + dx, z + dz));
                    return h >= 0.0f ? h : br.fluid_top;
                };
                for (auto& p : q)
                    if (std::fabs(p.y - box.to[1]) < 0.001f)
                        p.y = corner_top(p.x, p.z);
            }

            if (ft.cull && axis_aligned) {
                const int ax = kFaceAxis[eff_dir];
                f32 avg = 0;
                for (const auto& p : q) avg += ax == 0 ? p.x : (ax == 1 ? p.y : p.z);
                avg /= 4.0f;
                const bool on_border =
                    (eff_dir == FACE_WEST || eff_dir == FACE_DOWN || eff_dir == FACE_NORTH)
                        ? avg < 0.01f : avg > 15.99f;
                if (on_border) {
                    const i32 nx = x + kDX[eff_dir], ny = y + kDY[eff_dir], nz = z + kDZ[eff_dir];
                    const bool in_box = nx >= bb.x0 && nx <= bb.x1 && ny >= bb.y0 &&
                                        ny <= bb.y1 && nz >= bb.z0 && nz <= bb.z1;
                    const u16 n = in_box ? grid.at(nx, ny, nz)
                                         : std::numeric_limits<u16>::max();
                    if (in_box && occludes(n) && !(br.alpha_blend && n == grid.at(x, y, z)))
                        return;
                    if (in_box && br.alpha_blend && n == grid.at(x, y, z)) return;
                }
            } else if (ft.cull && !axis_aligned && br.alpha_blend) {
                const i32 nx = x + kDX[eff_dir], ny = y + kDY[eff_dir], nz = z + kDZ[eff_dir];
                const bool in_box = nx >= bb.x0 && nx <= bb.x1 && ny >= bb.y0 &&
                                    ny <= bb.y1 && nz >= bb.z0 && nz <= bb.z1;
                if (in_box && grid.at(nx, ny, nz) == grid.at(x, y, z)) return;
            }
            // Невидимые поверхности/кромки жидкостей не генерируем.
            if (br.fluid) {
                const i32 nx = x + kDX[eff_dir], ny = y + kDY[eff_dir], nz = z + kDZ[eff_dir];
                const bool in_box = nx >= bb.x0 && nx <= bb.x1 && ny >= bb.y0 &&
                                    ny <= bb.y1 && nz >= bb.z0 && nz <= bb.z1;
                if (in_box) {
                    const auto& nr = renders[grid.at(nx, ny, nz)];
                    // водозаполненная ячейка = вода
                    const bool same_fluid = nr.fluid && nr.fluid_kind == br.fluid_kind;
                    const bool same_waterlogged = br.fluid_kind == 1 && nr.waterlogged;
                    const f32 ntop = same_fluid ? nr.fluid_top
                                       : (same_waterlogged ? 16.0f : -1.0f);
                    if (ntop >= 0.0f) {
                        if (eff_dir == FACE_UP || eff_dir == FACE_DOWN) return;
                        if (ntop >= br.fluid_top - 0.001f) return;
                    } else if (eff_dir == FACE_DOWN && nr.full_opaque_cube) {
                        return;
                    }
                }
            }

            const auto& reg = atlas.region(ft.tile);
            auto AU = [&](f32 t) { return reg.u0 + (reg.u1 - reg.u0) * (t / 16.0f); };
            auto AV = [&](f32 t) { return reg.v0 + (reg.v1 - reg.v0) * (t / 16.0f); };

            f32 shade = ft.shade ? kFaceShade[eff_dir] : 1.0f;
            f32 col[3] = {shade, shade, shade};
            if (ft.tint) {
                f32 t0 = br.tint[0], t1 = br.tint[1], t2 = br.tint[2];
                // Биом-тон из colormap мира.
                if (br.tint_family && !grid.biome_pal.empty()) {
                    const u8 bid = grid.biome(x, y, z);
                    if (bid < grid.biome_pal.size()) {
                        const BiomeColors& bc = grid.biome_pal[bid];
                        const BiomeRGB& q = br.tint_family == 1   ? bc.grass
                                            : br.tint_family == 2 ? bc.foliage
                                                                  : bc.water;
                        t0 = q.r / 255.0f;
                        t1 = q.g / 255.0f;
                        t2 = q.b / 255.0f;
                    }
                }
                col[0] *= t0;
                col[1] *= t1;
                col[2] *= t2;
            }

            const f32* nrm = kFaceNormal[eff_dir];
            const u32 base = static_cast<u32>(out.vertices.size());
            for (const auto& p : q)
                out.vertices.push_back({x + p.x / 16.0f, y + p.y / 16.0f, z + p.z / 16.0f,
                                        AU(p.u), AV(p.v), col[0], col[1], col[2],
                                        nrm[0], nrm[1], nrm[2]});
            auto& idx = br.alpha_blend ? out.indices_alpha : out.indices;
            idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        };

        auto emit_cross = [&](const BlockRender& br, i32 x, i32 y, i32 z) {
            if (br.cross_tile < 0) return;
            const auto& reg = atlas.region(br.cross_tile);
            // Диагональ 0.05..0.95 блока.
            const f32 a = 0.05f, c = 0.95f;
            const f32 shade = 0.9f;
            f32 t0 = br.tint[0], t1 = br.tint[1], t2 = br.tint[2];
            // Биом-тон крестовых растений.
            if (br.tint_family && !grid.biome_pal.empty()) {
                const u8 bid = grid.biome(x, y, z);
                if (bid < grid.biome_pal.size()) {
                    const BiomeColors& bc = grid.biome_pal[bid];
                    const BiomeRGB& q = br.tint_family == 1   ? bc.grass
                                        : br.tint_family == 2 ? bc.foliage
                                                              : bc.water;
                    t0 = q.r / 255.0f;
                    t1 = q.g / 255.0f;
                    t2 = q.b / 255.0f;
                }
            }
            f32 col[3] = {shade * t0, shade * t1, shade * t2};
            const f32 planes[2][4] = {{a, a, c, c}, {a, c, c, a}};
            for (const auto& pl : planes) {
                const u32 base = static_cast<u32>(out.vertices.size());
                const f32 xs0 = x + pl[0], zs0 = z + pl[1];
                const f32 xs1 = x + pl[2], zs1 = z + pl[3];
                out.vertices.push_back({xs0, (f32)y,     zs0, reg.u0, reg.v1, col[0], col[1], col[2], 0, 1, 0});
                out.vertices.push_back({xs1, (f32)y,     zs1, reg.u1, reg.v1, col[0], col[1], col[2], 0, 1, 0});
                out.vertices.push_back({xs1, (f32)y + 1, zs1, reg.u1, reg.v0, col[0], col[1], col[2], 0, 1, 0});
                out.vertices.push_back({xs0, (f32)y + 1, zs0, reg.u0, reg.v0, col[0], col[1], col[2], 0, 1, 0});
                out.indices.insert(out.indices.end(),
                    {base, base + 1, base + 2, base, base + 2, base + 3});
            }
        };

        auto emit_bbquad = [&](const BlockRender& br, const BlockRender::Quad& qd,
                               i32 x, i32 y, i32 z) {
            if (qd.tile < 0) return;
            f32 p[4][3];
            std::memcpy(p, qd.p, sizeof(p));
            const int rx = norm_deg(static_cast<f32>(br.var_rx));
            const int ry = norm_deg(static_cast<f32>(br.var_ry));
            if (rx)
                for (auto& v : p)
                    for (int k = 0; k < rx / 90; ++k) rot_x90(v[1], v[2]);
            if (ry)
                for (auto& v : p)
                    for (int k = 0; k < ry / 90; ++k) rot_y90(v[0], v[2]);

            int dir = FACE_UP;
            {
                const f32 d1[3] = {p[0][0]-p[1][0], p[0][1]-p[1][1], p[0][2]-p[1][2]};
                const f32 d2[3] = {p[2][0]-p[1][0], p[2][1]-p[1][1], p[2][2]-p[1][2]};
                f32 cr[3] = {d2[1]*d1[2]-d2[2]*d1[1],
                             d2[2]*d1[0]-d2[0]*d1[2],
                             d2[0]*d1[1]-d2[1]*d1[0]};
                const f32 len = std::sqrt(cr[0]*cr[0] + cr[1]*cr[1] + cr[2]*cr[2]);
                if (len > 1e-6f) {
                    cr[0] /= len; cr[1] /= len; cr[2] /= len;
                    static const f32 ax[6][3] = {{-1,0,0},{1,0,0},{0,-1,0},
                                                 {0,1,0},{0,0,-1},{0,0,1}};
                    f32 best = 0.0f;
                    for (int d = 0; d < 6; ++d) {
                        const f32 dot = cr[0]*ax[d][0] + cr[1]*ax[d][1] + cr[2]*ax[d][2];
                        if (dot >= 0.0f && dot > best) { best = dot; dir = d; }
                    }
                }
            }

            const auto& reg = atlas.region(qd.tile);
            auto AU = [&](f32 t) { return reg.u0 + (reg.u1 - reg.u0) * (t / 16.0f); };
            auto AV = [&](f32 t) { return reg.v0 + (reg.v1 - reg.v0) * (t / 16.0f); };
            const f32 shade = qd.shade ? kFaceShade[dir] : 1.0f;
            const f32 col[3] = {shade, shade, shade};
            const f32* nrm = kFaceNormal[dir];
            const u32 base = static_cast<u32>(out.vertices.size());
            for (int i = 0; i < 4; ++i)
                out.vertices.push_back({x + p[i][0] / 16.0f, y + p[i][1] / 16.0f,
                                        z + p[i][2] / 16.0f,
                                        AU(qd.u[i]), AV(qd.v[i]), col[0], col[1], col[2],
                                        nrm[0], nrm[1], nrm[2]});
            auto& idx = br.alpha_blend ? out.indices_alpha : out.indices;
            idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        };

        for (i32 y = py0; y <= py1; ++y)
            for (i32 z = bb.z0; z <= bb.z1; ++z)
                for (i32 x = bb.x0; x <= bb.x1; ++x) {
                    const u16 id = grid.at(x, y, z);
                    if (!rendered(id)) continue;
                    const auto& br = renders[id];
                    ++out.quads_by_block[id];
                    if (br.cross) emit_cross(br, x, y, z);
                    else {
                        for (const auto& box : br.boxes) {
                            // Нулевая толщина по оси: оставляем одну из копланарных граней.
                            int skip = -1;
                            for (int a = 0; a < 3; ++a) {
                                if (std::fabs(box.from[a] - box.to[a]) < 0.001f) {
                                    const int fn = a * 2, fp = a * 2 + 1;
                                    const auto& A = box.faces[fn];
                                    const auto& B = box.faces[fp];
                                    if (A.tile >= 0 && A.tile == B.tile &&
                                        A.u0 == B.u0 && A.v0 == B.v0 &&
                                        A.u1 == B.u1 && A.v1 == B.v1)
                                        skip = fn;
                                }
                            }
                            for (int f = 0; f < 6; ++f)
                                if (f != skip) emit_face(br, box, f, x, y, z);
                        }
                        for (const auto& q : br.quads)
                            emit_bbquad(br, q, x, y, z);
                    }
                    // Вода в водозаполненной ячейке.
                    if (br.waterlogged && water_br && !opt.skip_fluids)
                        for (const auto& wb : water_br->boxes)
                            for (int f = 0; f < 6; ++f)
                                emit_face(*water_br, wb, f, x, y, z);
                }
    };

    if (pool && nparts > 1) {
        const i32 span = (y_hi - y_lo + 1 + nparts - 1) / nparts;
        std::vector<std::future<void>> futs;
        for (int p = 0; p < nparts; ++p) {
            const i32 a = y_lo + p * span;
            const i32 b2 = std::min(y_hi, a + span - 1);
            if (a > y_hi) break;
            futs.push_back(pool->submit(mesh_layer_range, p, a, b2));
        }
        for (auto& f : futs) f.get();
    } else {
        mesh_layer_range(0, y_lo, y_hi);
    }

    MCMesh m;
    size_t nv = 0, ni = 0, na = 0;
    for (auto& p : parts) { nv += p.vertices.size(); ni += p.indices.size(); na += p.indices_alpha.size(); }
    m.vertices.reserve(nv);
    m.indices.reserve(ni);
    m.indices_alpha.reserve(na);
    for (auto& p : parts) {
        const u32 base = static_cast<u32>(m.vertices.size());
        m.vertices.insert(m.vertices.end(), p.vertices.begin(), p.vertices.end());
        for (u32 i : p.indices) m.indices.push_back(base + i);
        for (u32 i : p.indices_alpha) m.indices_alpha.push_back(base + i);
    }
    m.quads_by_block.assign(renders.size(), 0);
    for (auto& p : parts)
        for (size_t i = 0; i < p.quads_by_block.size(); ++i)
            m.quads_by_block[i] += p.quads_by_block[i];
    return m;
}

}
