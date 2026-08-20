#include "block_model.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace vw::mods {

namespace {

std::string norm_ref(std::string s) {
    if (s.find(':') == std::string::npos) s = "minecraft:" + s;
    return s;
}

int face_index(std::string_view n) {
    if (n == "west") return FACE_WEST;
    if (n == "east") return FACE_EAST;
    if (n == "down") return FACE_DOWN;
    if (n == "up") return FACE_UP;
    if (n == "north") return FACE_NORTH;
    if (n == "south") return FACE_SOUTH;
    return -1;
}

void default_uv(int face, const f32 from[3], const f32 to[3],
                f32& u0, f32& v0, f32& u1, f32& v1) {
    const f32 x0 = from[0], y0 = from[1], z0 = from[2];
    const f32 x1 = to[0], y1 = to[1], z1 = to[2];
    switch (face) {
        case FACE_DOWN:  u0 = x0;     v0 = z0;     u1 = x1;     v1 = z1;     break;
        case FACE_UP:    u0 = x0;     v0 = z0;     u1 = x1;     v1 = z1;     break;
        case FACE_NORTH: u0 = 16 - x1; v0 = 16 - y1; u1 = 16 - x0; v1 = 16 - y0; break;
        case FACE_SOUTH: u0 = x0;     v0 = 16 - y1; u1 = x1;     v1 = 16 - y0; break;
        case FACE_WEST:  u0 = 16 - z1; v0 = 16 - y1; u1 = 16 - z0; v1 = 16 - y0; break;
        case FACE_EAST:  u0 = z0;     v0 = 16 - y1; u1 = z1;     v1 = 16 - y0; break;
        default:         u0 = 0; v0 = 0; u1 = 16; v1 = 16; break;
    }
}

}

std::string ModelResolver::resolve_tex(
    const std::map<std::string, std::string, std::less<>>& texmap, std::string ref) const {
    int hops = 0;
    while (ref.starts_with("#") && hops++ < 16) {
        auto it = texmap.find(ref.substr(1));
        if (it == texmap.end()) return {};
        ref = it->second;
    }
    if (ref.starts_with("#")) return {};
    return norm_ref(ref);
}

bool ModelResolver::collect(const std::string& model_ref,
                            std::map<std::string, std::string, std::less<>>& texmap,
                            json::Value& elements_out, std::string& root_parent, int depth) {
    if (depth > 24) return false;
    auto model = assets_.load_model_json(model_ref);
    if (!model.is_object()) {
        root_parent = model_ref;
        return false;
    }

    if (const auto* t = model.get("textures"); t && t->is_object())
        for (auto& [k, v] : *t->obj())
            if (v.is_string()) texmap.try_emplace(k, *v.str());

    if (elements_out.is_object() == false && !elements_out.is_array())
        if (const auto* e = model.get("elements"); e && e->is_array())
            elements_out = *e;
    if (const auto* p = model.get("parent"); p && p->is_string()) {
        root_parent = norm_ref(*p->str());
        collect(root_parent, texmap, elements_out, root_parent, depth + 1);
    }
    return true;
}

namespace {

bool prop_matches(const BlockState& bs, std::string_view key, std::string_view want) {
    std::string_view have;
    for (const auto& [k, v] : bs.props)
        if (k == key) { have = v; break; }
    if (have.empty()) return false;
    size_t pos = 0;
    while (pos <= want.size()) {
        const size_t bar = want.find('|', pos);
        const std::string_view alt = want.substr(pos, bar == std::string_view::npos
                                                           ? std::string_view::npos
                                                           : bar - pos);
        if (alt == have) return true;
        if (bar == std::string_view::npos) break;
        pos = bar + 1;
    }
    return false;
}

int variant_score(const BlockState& bs, std::string_view key) {
    if (key.empty()) return 0;
    int score = 0;
    size_t pos = 0;
    while (pos < key.size()) {
        const size_t comma = key.find(',', pos);
        const std::string_view cond =
            key.substr(pos, comma == std::string_view::npos ? std::string_view::npos
                                                            : comma - pos);
        const size_t eq = cond.find('=');
        if (eq == std::string_view::npos) return -1;
        if (!prop_matches(bs, cond.substr(0, eq), cond.substr(eq + 1))) return -1;
        ++score;
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
    return score;
}

bool when_matches(const BlockState& bs, const json::Value& when) {
    if (!when.is_object()) return true;
    if (const auto* orv = when.get("OR"); orv && orv->is_array()) {
        for (const auto& c : *orv->arr())
            if (when_matches(bs, c)) return true;
        return false;
    }
    if (const auto* andv = when.get("AND"); andv && andv->is_array()) {
        for (const auto& c : *andv->arr())
            if (!when_matches(bs, c)) return false;
        return true;
    }
    for (const auto& [k, v] : *when.obj()) {
        std::string want;
        if (v.is_string()) want = *v.str();
        else if (std::holds_alternative<bool>(v.v)) want = std::get<bool>(v.v) ? "true" : "false";
        else if (std::holds_alternative<double>(v.v)) {
            const double d = std::get<double>(v.v);
            want = std::to_string(static_cast<long long>(d));
        }
        if (!prop_matches(bs, k, want)) return false;
    }
    return true;
}

struct VariantPick { std::string model; int x = 0, y = 0; bool uvlock = false; };
std::optional<VariantPick> pick_variant(const json::Value& v) {
    const json::Value* m = v.is_array() && !v.arr()->empty() ? &v.arr()->front() : &v;
    const auto* mv = m->get("model");
    if (!mv || !mv->is_string()) return std::nullopt;
    VariantPick p;
    p.model = *mv->str();
    if (const auto* rx = m->get("x")) p.x = static_cast<int>(rx->num());
    if (const auto* ry = m->get("y")) p.y = static_cast<int>(ry->num());
    if (const auto* ul = m->get("uvlock"); ul && std::holds_alternative<bool>(ul->v))
        p.uvlock = std::get<bool>(ul->v);
    return p;
}

}

bool ModelResolver::append_model(const std::string& model_ref, int rot_x, int rot_y,
                                 bool uvlock, ResolvedModel& out) {
    std::map<std::string, std::string, std::less<>> texmap;
    json::Value elements;
    std::string root_parent;
    collect(norm_ref(model_ref), texmap, elements, root_parent, 0);

    // Крестовые растения: две диагональные плоскости.
    const bool cross_parent = root_parent.ends_with("block/cross") ||
                              root_parent.ends_with("block/tinted_cross");
    if (cross_parent) {
        if (out.boxes.empty() && !out.cross) {
            out.cross = true;
            out.cross_texture = resolve_tex(texmap, "#cross");
            if (out.cross_texture.empty() && !texmap.empty())
                out.cross_texture = resolve_tex(texmap, "#" + texmap.begin()->first);
        }
        return true;
    }

    if (elements.is_array()) {
        for (const auto& el : *elements.arr()) {
            ModelBox b;
            if (const auto* fr = el.get("from"); fr && fr->is_array() && fr->arr()->size() == 3)
                for (int i = 0; i < 3; ++i) b.from[i] = static_cast<f32>((*fr->arr())[i].num());
            if (const auto* to = el.get("to"); to && to->is_array() && to->arr()->size() == 3)
                for (int i = 0; i < 3; ++i) b.to[i] = static_cast<f32>((*to->arr())[i].num());

            if (const auto* rd = el.get("rotation"); rd && rd->is_object()) {
                if (const auto* ax = rd->get("axis"); ax && ax->is_string())
                    if (!ax->str()->empty()) b.rot_axis = (*ax->str())[0];
                if (const auto* an = rd->get("angle")) b.rot_angle = static_cast<f32>(an->num());
                if (const auto* og = rd->get("origin"); og && og->is_array() && og->arr()->size() == 3)
                    for (int i = 0; i < 3; ++i) b.rot_origin[i] = static_cast<f32>((*og->arr())[i].num());
                if (const auto* rs = rd->get("rescale"); rs && std::holds_alternative<bool>(rs->v))
                    b.rot_rescale = std::get<bool>(rs->v);
                b.has_rot = b.rot_angle != 0.0f;
            }

            b.var_rx = rot_x;
            b.var_ry = rot_y;
            b.var_uvlock = uvlock;

            bool el_shade = true;
            if (const auto* sh = el.get("shade"); sh && std::holds_alternative<bool>(sh->v))
                el_shade = std::get<bool>(sh->v);

            if (const auto* faces = el.get("faces"); faces && faces->is_object()) {
                for (auto& [fname, fdef] : *faces->obj()) {
                    const int fi = face_index(fname);
                    if (fi < 0) continue;
                    ModelFace mf;
                    mf.present = true;
                    mf.shade = el_shade;
                    if (const auto* tx = fdef.get("texture"); tx && tx->is_string())
                        mf.texture = resolve_tex(texmap, *tx->str());
                    if (const auto* uv = fdef.get("uv"); uv && uv->is_array() && uv->arr()->size() == 4) {
                        mf.u0 = static_cast<f32>((*uv->arr())[0].num());
                        mf.v0 = static_cast<f32>((*uv->arr())[1].num());
                        mf.u1 = static_cast<f32>((*uv->arr())[2].num());
                        mf.v1 = static_cast<f32>((*uv->arr())[3].num());
                    } else {
                        default_uv(fi, b.from, b.to, mf.u0, mf.v0, mf.u1, mf.v1);
                    }

                    if (const auto* frt = fdef.get("rotation"))
                        mf.uv_rot = ((static_cast<int>(frt->num()) % 360) + 360) % 360;
                    if (const auto* cf = fdef.get("cullface")) mf.cullface = !cf->is_string() || true;
                    if (const auto* ti = fdef.get("tintindex")) { (void)ti; mf.tint = true; }
                    if (const auto* sh2 = fdef.get("shade"); sh2 && std::holds_alternative<bool>(sh2->v))
                        mf.shade = std::get<bool>(sh2->v);
                    if (!mf.texture.empty()) b.faces[fi] = std::move(mf);
                }
            }
            out.boxes.push_back(std::move(b));
        }
    } else if (!texmap.empty()) {

        ModelBox b;
        b.var_rx = rot_x;
        b.var_ry = rot_y;
        b.var_uvlock = uvlock;
        static const char* slot_by_face[6] = {"west", "east", "down", "up", "north", "south"};
        for (int f = 0; f < 6; ++f) {
            std::string tex = resolve_tex(texmap, std::string("#") + slot_by_face[f]);
            if (tex.empty()) tex = resolve_tex(texmap, "#side");
            if (tex.empty() && (f == FACE_UP || f == FACE_DOWN)) {
                tex = resolve_tex(texmap, f == FACE_UP ? "#top" : "#bottom");
                if (tex.empty()) tex = resolve_tex(texmap, "#end");
            }
            if (tex.empty()) tex = resolve_tex(texmap, "#all");
            if (tex.empty()) tex = resolve_tex(texmap, "#texture");
            if (tex.empty() && !texmap.empty())
                tex = resolve_tex(texmap, "#" + texmap.begin()->first);
            if (!tex.empty()) {
                b.faces[f].present = true;
                b.faces[f].texture = std::move(tex);
                b.faces[f].cullface = true;
            }
        }
        out.boxes.push_back(std::move(b));
    }
    return false;
}

bool ModelResolver::append_custom_model(const BlockState& state, const std::string& model_ref,
                                        int rot_x, int rot_y, ResolvedModel& out) {
    const std::string& block_id = state.name;
    const std::string mref = norm_ref(model_ref);
    const std::string ns = mref.substr(0, mref.find(':'));

    std::string bb_ref, material;
    float scale = 1.0f;
    bool flip_v = false, custom = false;

    const json::Value mj = assets_.load_model_json(mref);
    if (mj.is_object()) {
        std::string loader;
        if (const auto* l = mj.get("loader"); l && l->is_string()) loader = *l->str();
        if (loader == "bbmodel" ||
            (loader.size() > 8 && loader.compare(loader.size() - 8, 8, ":bbmodel") == 0)) {
            custom = true;
            if (const auto* s = mj.get("scale")) scale = static_cast<float>(s->num(1.0));
            if (const auto* f = mj.get("flip-v"); f && std::holds_alternative<bool>(f->v))
                flip_v = std::get<bool>(f->v);
            if (const auto* m = mj.get("model"); m && m->is_string())
                bb_ref = m->str()->find(':') == std::string::npos ? ns + ":" + *m->str()
                                                                  : *m->str();
            if (const auto* m = mj.get("material"); m && m->is_string())
                material = norm_ref(*m->str());
        }
    }

    if (!custom)
        if (const auto de = assets_.find_deco_entry(block_id)) {
            custom = true;
            scale = de->scale;
            bb_ref = ns + ":models/bbmodel/" + de->model + ".bbmodel";
            material = ns + ":block/" + de->material;
        }
    if (!custom) return false;

    if (material.empty())
        if (auto t = assets_.resolve_block_texture(block_id)) material = *t;

    if (!bb_ref.empty()) {
        auto it = bb_cache_.find(bb_ref);
        if (it == bb_cache_.end()) {
            std::optional<BBModel> parsed;
            if (auto bytes = assets_.raw_bbmodel(bb_ref)) parsed = parse_bbmodel(*bytes);
            it = bb_cache_.emplace(bb_ref, std::move(parsed)).first;
        }
        if (it->second) {
            auto qs = bake_bbmodel(*it->second, scale, flip_v, material);
            if (!qs.empty()) {
                out.quads.insert(out.quads.end(),
                                 std::make_move_iterator(qs.begin()),
                                 std::make_move_iterator(qs.end()));
                if (out.var_rx == 0 && out.var_ry == 0) { out.var_rx = rot_x; out.var_ry = rot_y; }
                return true;
            }
        }
    }

    if (!material.empty()) {
        ModelBox b;
        b.var_rx = rot_x;
        b.var_ry = rot_y;
        for (int f = 0; f < 6; ++f) {
            b.faces[f].present = true;
            b.faces[f].texture = material;
            b.faces[f].cullface = true;
        }
        out.boxes.push_back(std::move(b));
    }
    return true;
}

ResolvedModel ModelResolver::build(const BlockState& state) {
    ResolvedModel out;
    const std::string& block_id = state.name;

    auto bs = assets_.load_blockstate_json(block_id);
    bool appended = false;
    if (bs.is_object()) {
        if (const auto* variants = bs.get("variants"); variants && variants->is_object()) {

            const json::Value* best = nullptr;
            int best_score = -1;
            for (auto& [k, v] : *variants->obj()) {
                const int s = variant_score(state, k);
                if (s > best_score) { best_score = s; best = &v; }
            }
            if (!best && !variants->obj()->empty()) best = &variants->obj()->begin()->second;
            if (best)
                if (auto p = pick_variant(*best)) {
                    append_custom_model(state, p->model, p->x, p->y, out) ||
                        append_model(p->model, p->x, p->y, p->uvlock, out);
                    appended = true;
                }
        } else if (const auto* multi = bs.get("multipart"); multi && multi->is_array()) {

            for (auto& part : *multi->arr()) {
                const auto* ap = part.get("apply");
                if (!ap) continue;
                if (const auto* when = part.get("when"); when && !when_matches(state, *when))
                    continue;
                if (auto p = pick_variant(*ap)) {
                    append_custom_model(state, p->model, p->x, p->y, out) ||
                        append_model(p->model, p->x, p->y, p->uvlock, out);
                    appended = true;
                }
            }

            if (!appended && !multi->arr()->empty()) {
                if (const auto* ap = multi->arr()->front().get("apply"))
                    if (auto p = pick_variant(*ap)) {
                        append_custom_model(state, p->model, p->x, p->y, out) ||
                            append_model(p->model, p->x, p->y, p->uvlock, out);
                        appended = true;
                    }
            }
        }
    }
    if (!appended) {

        const auto colon = block_id.find(':');
        append_model(block_id.substr(0, colon) + ":block/" + block_id.substr(colon + 1),
                     0, 0, false, out);
    }
    if (out.boxes.size() == 1) {
        const auto& b = out.boxes[0];
        out.full_cube = !b.has_rot && b.var_rx == 0 && b.var_ry == 0 &&
                        b.from[0] == 0 && b.from[1] == 0 && b.from[2] == 0 &&
                        b.to[0] == 16 && b.to[1] == 16 && b.to[2] == 16;
    }
    return out;
}

const ResolvedModel* ModelResolver::resolve_state(const BlockState& state) {
    const std::string key = state.key();
    std::lock_guard lk(mtx_);
    auto it = cache_.find(key);
    if (it == cache_.end())
        it = cache_.emplace(key, build(state)).first;
    return it->second.empty() ? nullptr : &it->second;
}

const ResolvedModel* ModelResolver::resolve(const std::string& block_id) {
    return resolve_state(BlockState{block_id, {}});
}

void ModelResolver::clear_cache() {
    std::lock_guard lk(mtx_);
    cache_.clear();
}

}
