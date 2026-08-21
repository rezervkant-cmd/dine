#include "mods/mod_assets.hpp"
#include <cstdio>
#include "nbt/nbt.hpp"
#include "core/platform.hpp"
#include <functional>
#include <algorithm>
#include <cstring>

namespace vw::mods {

namespace fs = std::filesystem;

namespace png {

static u32 be32(const u8* p) {
    return (u32(p[0]) << 24) | (u32(p[1]) << 16) | (u32(p[2]) << 8) | u32(p[3]);
}

static int paeth(int a, int b, int c) {
    const int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

std::optional<TextureRGBA> decode(std::span<const u8> file) {
    static const u8 magic[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (file.size() < 8 || std::memcmp(file.data(), magic, 8) != 0) return std::nullopt;

    u32 w = 0, h = 0;
    u8 bit_depth = 0, color_type = 0, interlace = 0;
    std::vector<u8> idat;
    std::vector<std::array<u8, 4>> pal;

    size_t p = 8;
    while (p + 8 <= file.size()) {
        const u32 len = be32(&file[p]);
        if (p + 12 + len > file.size()) break;
        const char* type = reinterpret_cast<const char*>(&file[p + 4]);
        const u8* d = &file[p + 8];
        if (!std::memcmp(type, "IHDR", 4)) {
            if (len != 13) return std::nullopt;
            w = be32(d); h = be32(d + 4);
            bit_depth = d[8]; color_type = d[9]; interlace = d[12];
        } else if (!std::memcmp(type, "PLTE", 4)) {
            for (u32 i = 0; i + 2 < len; i += 3) pal.push_back({d[i], d[i+1], d[i+2], 255});
        } else if (!std::memcmp(type, "tRNS", 4) && color_type == 3) {
            for (u32 i = 0; i < len && i < pal.size(); ++i) pal[i][3] = d[i];
        } else if (!std::memcmp(type, "IDAT", 4)) {
            idat.insert(idat.end(), d, d + len);
        } else if (!std::memcmp(type, "IEND", 4)) break;
        p += 12 + len;
    }
    if (!w || !h || interlace != 0 || idat.empty()) return std::nullopt;

    int channels;
    switch (color_type) {
        case 0: channels = 1; break; case 2: channels = 3; break; case 3: channels = 1; break;
        case 4: channels = 2; break; case 6: channels = 4; break; default: return std::nullopt;
    }

    if (bit_depth != 8) {
        if (!((color_type == 0 || color_type == 3) &&
              (bit_depth == 1 || bit_depth == 2 || bit_depth == 4)))
            return std::nullopt;
        if (color_type == 3 && pal.empty()) return std::nullopt;
    }

    std::vector<u8> raw;
    try { raw = nbt::inflate(idat, true); } catch (...) { return std::nullopt; }

    const size_t stride = (static_cast<size_t>(w) * channels * bit_depth + 7) / 8;
    const int ch = std::max(1, channels * bit_depth / 8);
    if (raw.size() < (stride + 1) * h) return std::nullopt;

    std::vector<u8> img(stride * h);
    for (u32 y = 0; y < h; ++y) {
        const u8 filt = raw[y * (stride + 1)];
        const u8* src = &raw[y * (stride + 1) + 1];
        u8* dst = &img[y * stride];
        const u8* up = y ? &img[(y - 1) * stride] : nullptr;
        for (size_t x = 0; x < stride; ++x) {
            const int a = x >= size_t(ch) ? dst[x - ch] : 0;
            const int b = up ? up[x] : 0;
            const int c = (up && x >= size_t(ch)) ? up[x - ch] : 0;
            int v = src[x];
            switch (filt) {
                case 1: v += a; break;
                case 2: v += b; break;
                case 3: v += (a + b) / 2; break;
                case 4: v += paeth(a, b, c); break;
            }
            dst[x] = static_cast<u8>(v);
        }
    }

    TextureRGBA out;
    out.w = static_cast<int>(w); out.h = static_cast<int>(h);
    out.pixels.resize(static_cast<size_t>(w) * h * 4);

    const int bpp = channels * bit_depth;
    auto sub_pixel = [&](size_t y, size_t x) -> u8 {
        const u8* row = &img[y * stride];
        const size_t bitpos = x * static_cast<size_t>(bpp);
        const u8 byte = row[bitpos >> 3];
        const int shift = 8 - bpp - static_cast<int>(bitpos & 7);
        return static_cast<u8>((byte >> shift) & ((1 << bpp) - 1));
    };

    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            u8 r, g, b, a = 255;
            if (bit_depth != 8) {
                const u8 val = sub_pixel(y, x);
                if (color_type == 3) {
                    const auto& c = val < pal.size() ? pal[val] : std::array<u8,4>{0,0,0,255};
                    r = c[0]; g = c[1]; b = c[2]; a = c[3];
                } else {

                    const u8 sc = static_cast<u8>(val * 255 / ((1 << bpp) - 1));
                    r = g = b = sc;
                }
            } else {
                const u8* s = &img[(static_cast<size_t>(y) * w + x) * channels];
                switch (color_type) {
                    case 0: r = g = b = s[0]; break;
                    case 2: r = s[0]; g = s[1]; b = s[2]; break;
                    case 3: {
                        const auto& c = s[0] < pal.size() ? pal[s[0]] : std::array<u8,4>{0,0,0,255};
                        r = c[0]; g = c[1]; b = c[2]; a = c[3];
                        break;
                    }
                    case 4: r = g = b = s[0]; a = s[1]; break;
                    default: r = s[0]; g = s[1]; b = s[2]; a = s[3];
                }
            }
            u8* d2 = &out.pixels[(static_cast<size_t>(y) * w + x) * 4];
            d2[0] = r; d2[1] = g; d2[2] = b; d2[3] = a;
        }
    return out;
}

}

namespace {

struct ParsedAsset {
    enum Kind { None, Blockstate, Model, Texture, BBModel } kind = None;
    std::string key;
    std::string modid;
};

ParsedAsset classify(const std::string& path) {
    ParsedAsset r;
    if (!path.starts_with("assets/")) return r;
    const size_t m0 = 7;
    const size_t m1 = path.find('/', m0);
    if (m1 == std::string::npos) return r;
    r.modid = path.substr(m0, m1 - m0);
    const std::string_view rest(path.data() + m1 + 1, path.size() - m1 - 1);

    auto strip = [&](std::string_view prefix, std::string_view ext) -> std::string {
        std::string_view s = rest;
        s.remove_prefix(prefix.size());
        s.remove_suffix(ext.size());
        return std::string(s);
    };
    if (rest.starts_with("blockstates/") && rest.ends_with(".json")) {
        r.kind = ParsedAsset::Blockstate;
        r.key = r.modid + ":" + strip("blockstates/", ".json");
    } else if (rest.starts_with("models/") && rest.ends_with(".json")) {
        r.kind = ParsedAsset::Model;
        r.key = r.modid + ":" + strip("models/", ".json");
    } else if (rest.starts_with("textures/") && rest.ends_with(".png")) {
        r.kind = ParsedAsset::Texture;
        r.key = r.modid + ":" + strip("textures/", ".png");
    } else if (rest.starts_with("models/") && rest.ends_with(".bbmodel")) {
        r.kind = ParsedAsset::BBModel;
        r.key = r.modid + ":" + std::string(rest);
    }
    return r;
}

std::string norm_ref(std::string s, const std::string& def_ns = "minecraft") {
    if (s.find(':') == std::string::npos) s = def_ns + ":" + s;
    return s;
}

}

void AssetRegistry::scan_jar(const fs::path& jar) {
    auto zf = std::make_unique<zip::ZipFile>(jar);
    ModInfo mi;
    mi.file_name = vw::plat::u8str(jar.filename());
    if (zf->find("fabric.mod.json")) mi.loader = "fabric";
    else if (zf->find("META-INF/mods.toml") || zf->find("META-INF/neoforge.mods.toml"))
        mi.loader = "forge/neoforge";
    else mi.loader = "unknown";

    std::lock_guard lk(mtx_);
    const size_t ji = jars_.size();
    std::vector<std::string> ids;
    for (const auto& e : zf->entries()) {

        if (e.name.ends_with(".png.mcmeta") && e.name.starts_with("assets/")) {
            const size_t m1 = e.name.find('/', 7);
            if (m1 != std::string::npos) {
                const std::string_view rest(e.name.data() + m1 + 1,
                                            e.name.size() - m1 - 1);
                if (rest.starts_with("textures/")) {
                    if (auto meta = zf->read(e.name)) {
                        const std::string_view sv(
                            reinterpret_cast<const char*>(meta->data()), meta->size());
                        if (sv.find("\"animation\"") != std::string_view::npos) {
                            const std::string key =
                                e.name.substr(7, m1 - 7) + ":" +
                                std::string(rest.substr(9, rest.size() - 9 - 11));
                            animated_[key] = true;
                        }
                    }
                }
            }
        }
        auto pa = classify(e.name);
        if (pa.kind == ParsedAsset::None) continue;
        if (std::find(ids.begin(), ids.end(), pa.modid) == ids.end()) ids.push_back(pa.modid);
        AssetRef ref{ji, e.name};
        switch (pa.kind) {
            case ParsedAsset::Blockstate: blockstates_[pa.key] = ref; ++mi.blockstates; break;
            case ParsedAsset::Model:      models_[pa.key] = ref;      ++mi.models; break;
            case ParsedAsset::Texture:    textures_[pa.key] = ref;    ++mi.textures; break;
            case ParsedAsset::BBModel:    bbmodels_[pa.key] = ref;    break;
            default: break;
        }
    }
    mi.mod_ids = std::move(ids);
    jars_.push_back(std::move(zf));
    mods_.push_back(std::move(mi));

    block_tex_cache_.clear();
    color_cache_.clear();
    deco_regs_.clear();
}

void AssetRegistry::clear() {
    std::lock_guard lk(mtx_);
    jars_.clear();
    mods_.clear();
    blockstates_.clear();
    models_.clear();
    textures_.clear();
    bbmodels_.clear();
    animated_.clear();
    deco_regs_.clear();
    block_tex_cache_.clear();
    color_cache_.clear();
}

void AssetRegistry::scan_mods_dir(const fs::path& mods_dir, ThreadPool& pool) {
    std::vector<fs::path> jars;
    if (fs::exists(mods_dir))
        for (auto& e : fs::directory_iterator(mods_dir))
            if (e.path().extension() == ".jar") jars.push_back(e.path());

    pool.parallel_for(0, jars.size(), [&](size_t i) {
        try { scan_jar(jars[i]); } catch (const std::exception& e) {
            std::fprintf(stderr, "dine: scan jar %s failed: %s\n", vw::plat::u8str(jars[i]).c_str(), e.what());
        } catch (...) {
            std::fprintf(stderr, "dine: scan jar %s failed (unknown)\n", vw::plat::u8str(jars[i]).c_str());
        }
    });
}

json::Value AssetRegistry::load_json_asset(
    const std::unordered_map<std::string, AssetRef>& table, const std::string& key) {
    // Мьютекс держим и во время чтения из JAR: clear() из другого потока
    // уничтожает ZipFile, поэтому отпускать блокировку до использования
    // указателя нельзя (use-after-free).
    std::lock_guard lk(mtx_);
    auto it = table.find(key);
    if (it == table.end()) return {};
    if (it->second.jar_index >= jars_.size()) return {};
    auto bytes = jars_[it->second.jar_index]->read(it->second.path);
    if (!bytes) return {};
    try { return json::parse(*bytes); } catch (...) { return {}; }
}

std::optional<std::string> AssetRegistry::resolve_model_texture(
    const std::string& model_ref, int depth) {
    if (depth > 16) return std::nullopt;
    auto model = load_json_asset(models_, model_ref);
    if (!model.is_object()) return std::nullopt;

    if (const auto* m = model.get("material"); m && m->is_string()) {
        const std::string ref = norm_ref(*m->str());
        if (has_texture(ref)) return ref;
    }

    std::map<std::string, std::string, std::less<>> texmap;
    if (const auto* t = model.get("textures"); t && t->is_object())
        for (auto& [k, v] : *t->obj())
            if (v.is_string()) texmap[k] = *v.str();

    for (const char* slot : {"all", "top", "side", "end", "texture", "up", "north", "particle"}) {
        auto it = texmap.find(slot);
        if (it == texmap.end()) continue;
        std::string ref = it->second;

        int hop = 0;
        while (ref.starts_with("#") && hop++ < 8) {
            auto jt = texmap.find(ref.substr(1));
            if (jt == texmap.end()) break;
            ref = jt->second;
        }
        if (!ref.starts_with("#")) return norm_ref(ref);
    }

    for (auto& [k, v] : texmap)
        if (!v.starts_with("#")) return norm_ref(v);

    if (const auto* par = model.get("parent"); par && par->is_string())
        return resolve_model_texture(norm_ref(*par->str()), depth + 1);
    return std::nullopt;
}

std::optional<std::string> AssetRegistry::resolve_block_texture(const std::string& block_id) {
    {
        std::lock_guard lk(mtx_);
        if (auto it = block_tex_cache_.find(block_id); it != block_tex_cache_.end())
            return it->second.empty() ? std::nullopt : std::make_optional(it->second);
    }
    std::optional<std::string> result;

    if (const auto de = find_deco_entry(block_id)) {
        const std::string tex =
            block_id.substr(0, block_id.find(':')) + ":block/" + de->material;
        if (has_texture(tex)) result = tex;
    }

    auto bs = load_json_asset(blockstates_, block_id);
    if (bs.is_object()) {
        std::string model_ref;
        if (const auto* variants = bs.get("variants"); variants && variants->is_object()) {

            for (auto& [k, v] : *variants->obj()) {
                const json::Value* m = v.is_array() && !v.arr()->empty() ? &v.arr()->front() : &v;
                if (const auto* mv = m->get("model"); mv && mv->is_string()) {
                    model_ref = *mv->str();
                    break;
                }
            }
        } else if (const auto* multi = bs.get("multipart"); multi && multi->is_array()) {
            for (auto& part : *multi->arr()) {
                const auto* ap = part.get("apply");
                if (!ap) continue;
                const json::Value* m = ap->is_array() && !ap->arr()->empty() ? &ap->arr()->front() : ap;
                if (const auto* mv = m->get("model"); mv && mv->is_string()) {
                    model_ref = *mv->str();
                    break;
                }
            }
        }
        if (!result && !model_ref.empty())
            result = resolve_model_texture(norm_ref(model_ref), 0);
    }

    if (!result) {
        // Если namespace отсутствует (block_id без ':'), подставляем minecraft: —
        // иначе substr(npos + 1) == substr(0) и получался бы мусор "id:block/id".
        const auto colon = block_id.find(':');
        const std::string guess =
            colon == std::string::npos
                ? "minecraft:block/" + block_id
                : block_id.substr(0, colon) + ":block/" + block_id.substr(colon + 1);
        if (has_texture(guess)) result = guess;
    }

    std::lock_guard lk(mtx_);
    block_tex_cache_[block_id] = result.value_or("");
    return result;
}

std::optional<std::vector<u8>> AssetRegistry::raw_texture_png(const std::string& tex_ref) {
    std::lock_guard lk(mtx_);
    auto it = textures_.find(tex_ref);
    if (it == textures_.end()) return std::nullopt;
    if (it->second.jar_index >= jars_.size()) return std::nullopt;
    return jars_[it->second.jar_index]->read(it->second.path);
}

std::optional<std::vector<u8>> AssetRegistry::raw_bbmodel(const std::string& key) {
    std::lock_guard lk(mtx_);
    auto it = bbmodels_.find(key);
    if (it == bbmodels_.end()) return std::nullopt;
    if (it->second.jar_index >= jars_.size()) return std::nullopt;
    return jars_[it->second.jar_index]->read(it->second.path);
}

bool AssetRegistry::has_animation_meta(const std::string& tex_ref) const {
    std::lock_guard lk(mtx_);
    return animated_.count(tex_ref) != 0;
}

void AssetRegistry::load_deco_registry(const std::string& ns) {
    std::map<std::string, DecoEntry> reg;
    std::lock_guard lk(mtx_);   // рекурсивный: перебор jars_ + запись deco_regs_
    for (auto& zf : jars_) {
        const std::string index_path = "assets/" + ns + "/decocraft.json";
        auto idx_bytes = zf->read(index_path);
        if (!idx_bytes) continue;
        json::Value idx;
        try { idx = json::parse(*idx_bytes); } catch (...) { continue; }
        if (!idx.is_array()) continue;
        for (const auto& fn : *idx.arr()) {
            if (!fn.is_string()) continue;
            auto jb = zf->read("assets/" + ns + "/" + *fn.str());
            if (!jb) continue;
            json::Value doc;
            try { doc = json::parse(*jb); } catch (...) { continue; }
            const auto* models = doc.get("models");
            if (!models || !models->is_array()) continue;
            for (const auto& ent : *models->arr()) {
                if (!ent.is_object()) continue;
                DecoEntry e;
                std::string key;
                if (const auto* v = ent.get("model"); v && v->is_string()) e.model = *v->str();
                if (const auto* v = ent.get("decoref"); v && v->is_string()) key = *v->str();
                if (const auto* v = ent.get("material"); v && v->is_string()) e.material = *v->str();
                if (const auto* v = ent.get("scale")) e.scale = static_cast<float>(v->num(1.0));
                if (key.empty()) key = e.material.empty() ? e.model : e.material;
                if (e.material.empty()) e.material = key;
                if (key.empty() || e.model.empty()) continue;
                reg.try_emplace(std::move(key), std::move(e));
            }
        }
    }
    deco_regs_[ns] = {true, std::move(reg)};
}

std::optional<AssetRegistry::DecoEntry> AssetRegistry::find_deco_entry(
    const std::string& block_id) {
    const auto colon = block_id.find(':');
    if (colon == std::string::npos) return std::nullopt;
    const std::string ns = block_id.substr(0, colon);
    const std::string name = block_id.substr(colon + 1);
    {
        std::lock_guard lk(mtx_);
        if (auto it = deco_regs_.find(ns); it != deco_regs_.end()) {
            auto eit = it->second.second.find(name);
            if (eit == it->second.second.end()) return std::nullopt;
            return eit->second;
        }
    }

    load_deco_registry(ns);
    std::lock_guard lk(mtx_);
    auto it = deco_regs_.find(ns);
    if (it == deco_regs_.end()) return std::nullopt;
    auto eit = it->second.second.find(name);
    if (eit == it->second.second.end()) return std::nullopt;
    return eit->second;
}

std::optional<TextureRGBA> AssetRegistry::load_texture(const std::string& tex_ref) {
    auto bytes = raw_texture_png(tex_ref);
    if (!bytes) return std::nullopt;
    std::optional<TextureRGBA> tex;
    if (decoder_) tex = decoder_(*bytes);
    if (!tex) tex = png::decode(*bytes);
    if (tex) {
        tex->source = tex_ref;
        tex->animated = has_animation_meta(tex_ref);
    }
    return tex;
}

std::optional<std::array<u8, 4>> AssetRegistry::average_color(const std::string& block_id) {

    {
        std::string_view n(block_id);
        if (auto c = n.find(':'); c != std::string_view::npos) n.remove_prefix(c + 1);
        if (n == "barrier") return std::array<u8, 4>{219, 33, 26, 255};
    }
    {
        std::lock_guard lk(mtx_);
        if (auto it = color_cache_.find(block_id); it != color_cache_.end()) return it->second;
    }
    auto ref = resolve_block_texture(block_id);
    if (!ref) return std::nullopt;
    auto tex = load_texture(*ref);
    if (!tex) return std::nullopt;
    u64 r = 0, g = 0, b = 0, a = 0, n = 0;
    for (size_t i = 0; i < tex->pixels.size(); i += 4) {
        if (tex->pixels[i + 3] < 8) continue;
        r += tex->pixels[i]; g += tex->pixels[i + 1]; b += tex->pixels[i + 2];
        a += tex->pixels[i + 3];
        ++n;
    }
    if (!n) return std::nullopt;
    std::array<u8, 4> c{static_cast<u8>(r / n), static_cast<u8>(g / n),
                        static_cast<u8>(b / n), static_cast<u8>(a / n)};
    std::lock_guard lk(mtx_);
    color_cache_[block_id] = c;
    return c;
}

}
