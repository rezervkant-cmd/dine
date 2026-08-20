#include "bbmodel.hpp"
#include "mods/json.hpp"
#include "mods/block_model.hpp"
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <unordered_map>

namespace vw::mods {

namespace {

constexpr float DEG2RAD = 0.017453292519943295f;

const uint8_t kFaceInfo[6][4][3] = {

    {{0, 4, 5}, {0, 1, 5}, {0, 1, 2}, {0, 4, 2}},

    {{3, 4, 2}, {3, 1, 2}, {3, 1, 5}, {3, 4, 5}},

    {{0, 1, 2}, {0, 1, 5}, {3, 1, 5}, {3, 1, 2}},

    {{0, 4, 2}, {0, 4, 5}, {3, 4, 5}, {3, 4, 2}},

    {{3, 4, 2}, {3, 1, 2}, {0, 1, 2}, {0, 4, 2}},

    {{0, 4, 5}, {0, 1, 5}, {3, 1, 5}, {3, 4, 5}},
};

int face_index(std::string_view n) {
    if (n == "west") return FACE_WEST;
    if (n == "east") return FACE_EAST;
    if (n == "down") return FACE_DOWN;
    if (n == "up") return FACE_UP;
    if (n == "north") return FACE_NORTH;
    if (n == "south") return FACE_SOUTH;
    return -1;
}

bool json_vec3(const json::Value& v, float out[3]) {
    if (!v.is_array() || v.arr()->size() < 3) return false;
    for (int i = 0; i < 3; ++i) out[i] = static_cast<float>((*v.arr())[i].num());
    return true;
}

bool json_bool(const json::Value* p, bool def) {
    if (!p || !std::holds_alternative<bool>(p->v)) return def;
    return std::get<bool>(p->v);
}

void rot_axis(float p[3], char axis, float deg) {
    const float c = std::cos(deg * DEG2RAD), s = std::sin(deg * DEG2RAD);
    if (axis == 'x') {
        const float y = p[1], z = p[2];
        p[1] = y * c - z * s;
        p[2] = y * s + z * c;
    } else if (axis == 'y') {
        const float x = p[0], z = p[2];
        p[0] = x * c + z * s;
        p[2] = -x * s + z * c;
    } else {
        const float x = p[0], y = p[1];
        p[0] = x * c - y * s;
        p[1] = x * s + y * c;
    }
}

void apply_rotation(float p[3], const float origin[3], const float rot[3]) {
    static const char kAxes[3] = {'x', 'y', 'z'};
    for (int a = 0; a < 3; ++a) {
        if (rot[a] == 0.0f) continue;
        float q[3] = {p[0] - origin[0], p[1] - origin[1], p[2] - origin[2]};
        rot_axis(q, kAxes[a], rot[a]);
        p[0] = q[0] + origin[0];
        p[1] = q[1] + origin[1];
        p[2] = q[2] + origin[2];
    }
}

}

std::optional<BBModel> parse_bbmodel(std::span<const u8> bytes) {
    json::Value root;
    try { root = json::parse(bytes); } catch (...) { return std::nullopt; }
    if (!root.is_object()) return std::nullopt;

    BBModel m;
    if (const auto* r = root.get("resolution"); r && r->is_object()) {
        if (const auto* w = r->get("width"))  m.res_w = std::max(1, static_cast<int>(w->num()));
        if (const auto* h = r->get("height")) m.res_h = std::max(1, static_cast<int>(h->num()));
    }

    std::unordered_map<std::string, size_t> order;
    const auto* els = root.get("elements");
    if (!els || !els->is_array()) return std::nullopt;
    for (const auto& el : *els->arr()) {
        if (!el.is_object()) continue;
        std::string type = "cube";
        if (const auto* t = el.get("type"); t && t->is_string()) type = *t->str();
        std::string name;
        if (const auto* n = el.get("name"); n && n->is_string()) name = *n->str();

        if (type != "cube") {

            if (type == "locator") {
                std::string low = name;
                for (auto& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (low == "root_node") {
                    m.has_root = true;
                    if (const auto* p = el.get("position")) json_vec3(*p, m.root_pos);
                    else if (const auto* f = el.get("from")) json_vec3(*f, m.root_pos);
                }
            }
            continue;
        }
        if (json_bool(el.get("visibility"), true) == false) continue;
        if (json_bool(el.get("export"), true) == false) continue;

        BBModel::Cube c;
        if (const auto* v = el.get("from")) json_vec3(*v, c.from);
        if (const auto* v = el.get("to")) json_vec3(*v, c.to);
        if (const auto* v = el.get("origin")) json_vec3(*v, c.origin);
        if (const auto* v = el.get("rotation")) json_vec3(*v, c.rot);
        if (const auto* v = el.get("inflate")) c.inflate = static_cast<float>(v->num());
        c.shade = json_bool(el.get("shade"), true);

        if (const auto* faces = el.get("faces"); faces && faces->is_object()) {
            static const char* kDirs[6] = {"west", "east", "down", "up", "north", "south"};
            for (const char* dn : kDirs) {
                const auto* f = faces->get(dn);
                if (!f || !f->is_object()) continue;

                if (const auto* tv = f->get("texture");
                    tv && std::holds_alternative<std::nullptr_t>(tv->v)) continue;
                const auto* uv = f->get("uv");
                if (!uv || !uv->is_array() || uv->arr()->size() < 4) continue;
                BBModel::Cube::Face bf;
                bf.dir = face_index(dn);
                bf.u0 = static_cast<float>((*uv->arr())[0].num());
                bf.v0 = static_cast<float>((*uv->arr())[1].num());
                bf.u1 = static_cast<float>((*uv->arr())[2].num());
                bf.v1 = static_cast<float>((*uv->arr())[3].num());
                c.faces.push_back(bf);
            }
        }
        if (c.faces.empty()) continue;

        std::string uuid = name;
        if (const auto* u = el.get("uuid"); u && u->is_string()) uuid = *u->str();
        order[uuid] = m.cubes.size();
        m.cubes.push_back(std::move(c));
    }

    std::unordered_map<std::string, std::vector<BBModel::Cube::Group>> chains;
    std::function<void(const json::Value&, std::vector<BBModel::Cube::Group>&)> walk =
        [&](const json::Value& items, std::vector<BBModel::Cube::Group>& chain) {
            if (!items.is_array()) return;
            for (const auto& it : *items.arr()) {
                if (it.is_string()) {
                    chains[*it.str()] = chain;
                } else if (it.is_object()) {
                    BBModel::Cube::Group g{{0, 0, 0}, {0, 0, 0}};
                    if (const auto* o = it.get("origin")) json_vec3(*o, g.origin);
                    if (const auto* r = it.get("rotation")) json_vec3(*r, g.rot);
                    chain.push_back(g);
                    if (const auto* ch = it.get("children")) walk(*ch, chain);
                    chain.pop_back();
                }
            }
        };
    if (const auto* out = root.get("outliner")) {
        std::vector<BBModel::Cube::Group> chain;
        walk(*out, chain);
    }

    for (auto& [uuid, chain] : chains)
        if (auto it = order.find(uuid); it != order.end())
            m.cubes[it->second].groups = std::move(chain);

    return m;
}

std::vector<BBQuad> bake_bbmodel(const BBModel& m, float scale, bool flip_v,
                                 const std::string& material) {
    std::vector<BBQuad> out;
    const float rw = static_cast<float>(m.res_w) / 16.0f;
    const float rh = static_cast<float>(m.res_h) / 16.0f;

    for (const auto& c : m.cubes) {

        float shape[6];
        for (int i = 0; i < 3; ++i) shape[i] = (c.from[i] - c.inflate / 2.0f) / 16.0f;
        for (int i = 0; i < 3; ++i) shape[3 + i] = (c.to[i] + c.inflate / 2.0f) / 16.0f;
        const float eorg[3] = {c.origin[0] / 16.0f, c.origin[1] / 16.0f, c.origin[2] / 16.0f};

        for (const auto& f : c.faces) {
            BBQuad q;
            q.shade = c.shade;
            q.texture = material;
            for (int i = 0; i < 4; ++i) {
                float p[3] = {shape[kFaceInfo[f.dir][i][0]],
                              shape[kFaceInfo[f.dir][i][1]],
                              shape[kFaceInfo[f.dir][i][2]]};

                apply_rotation(p, eorg, c.rot);
                for (auto gi = c.groups.rbegin(); gi != c.groups.rend(); ++gi) {
                    const float gorg[3] = {gi->origin[0] / 16.0f, gi->origin[1] / 16.0f,
                                           gi->origin[2] / 16.0f};
                    apply_rotation(p, gorg, gi->rot);
                }

                for (int a = 0; a < 3; ++a) p[a] *= scale;
                p[0] += 0.5f;
                p[2] += 0.5f;
                if (m.has_root)
                    for (int a = 0; a < 3; ++a) p[a] -= m.root_pos[a] / 16.0f;

                q.p[i][0] = p[0] * 16.0f;
                q.p[i][1] = p[1] * 16.0f;
                q.p[i][2] = p[2] * 16.0f;
            }

            const float uu[4] = {f.u0, f.u0, f.u1, f.u1};
            const float vv_nf[4] = {f.v0, f.v1, f.v1, f.v0};
            const float vv_f[4] = {f.v1, f.v0, f.v0, f.v1};
            const float* vv = flip_v ? vv_f : vv_nf;
            for (int i = 0; i < 4; ++i) {
                q.u[i] = uu[i] / rw;
                q.v[i] = vv[i] / rh;
            }
            out.push_back(std::move(q));
        }
    }
    return out;
}

}
