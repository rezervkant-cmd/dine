#include "world.hpp"
#include "core/biome_colors.hpp"
#include "core/legacy_ids.hpp"
#include "core/platform.hpp"
#include <atomic>
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>

namespace vw::anvil {

namespace fs = std::filesystem;

namespace {

u64 region_key(i32 rx, i32 rz) {
    return (static_cast<u64>(static_cast<u32>(rx)) << 32) | static_cast<u32>(rz);
}

std::vector<BlockState> read_palette(const nbt::List& pal) {
    std::vector<BlockState> out;
    out.reserve(pal.items.size());
    for (const auto& e : pal.items) {
        BlockState bs;
        if (const auto* c = e.as_compound()) {
            if (auto it = c->find("Name"); it != c->end())
                if (auto* s = it->second.as_string()) bs.name = *s;
            if (auto it = c->find("Properties"); it != c->end())
                if (const auto* pc = it->second.as_compound())
                    for (auto& [k, v] : *pc)
                        if (auto* s = v.as_string()) bs.props.emplace_back(k, *s);
        }
        if (bs.name.empty()) bs.name = "minecraft:air";
        out.push_back(std::move(bs));
    }
    return out;
}

std::vector<u16> unpack_indices(std::span<const i64> data, u32 bits, size_t count, bool padded) {
    std::vector<u16> out(count, 0);
    const u64 mask = (bits >= 64) ? ~0ull : ((1ull << bits) - 1);
    if (padded) {
        const u32 per_long = 64 / bits;
        for (size_t i = 0; i < count; ++i) {
            const size_t l = i / per_long;
            if (l >= data.size()) break;
            const u32 shift = static_cast<u32>(i % per_long) * bits;
            out[i] = static_cast<u16>((static_cast<u64>(data[l]) >> shift) & mask);
        }
    } else {
        for (size_t i = 0; i < count; ++i) {
            const size_t bit = i * bits;
            const size_t l = bit >> 6;
            const u32 off = bit & 63;
            if (l >= data.size()) break;
            u64 v = static_cast<u64>(data[l]) >> off;
            if (off + bits > 64 && l + 1 < data.size())
                v |= static_cast<u64>(data[l + 1]) << (64 - off);
            out[i] = static_cast<u16>(v & mask);
        }
    }
    return out;
}

u8 nibble(std::span<const i8> arr, size_t i) {
    const u8 b = static_cast<u8>(arr[i >> 1]);
    return (i & 1) ? (b >> 4) : (b & 0x0f);
}

std::filesystem::path dimension_region_dir(const fs::path& root, std::string_view name) {
    if (name == "overworld") return root / "region";
    if (name == "the_nether") return root / "DIM-1" / "region";
    if (name == "the_end") return root / "DIM1" / "region";
    return root / "dimensions" / fs::path(name) / "region";
}

std::vector<std::string> find_dimensions(const fs::path& root) {
    std::vector<std::string> out;
    auto add_if_region = [&](std::string name) {
        if (fs::is_directory(dimension_region_dir(root, name))) out.push_back(std::move(name));
    };
    add_if_region("overworld");
    add_if_region("the_nether");
    add_if_region("the_end");
    const fs::path custom = root / "dimensions";
    std::error_code ec;
    if (fs::is_directory(custom, ec)) {
        for (fs::recursive_directory_iterator it(custom, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_directory(ec) || it->path().filename() != "region") continue;
            const fs::path parent = it->path().parent_path();
            const fs::path rel = fs::relative(parent, custom, ec);
            if (!ec && !rel.empty()) out.push_back(rel.generic_string());
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}

World::World(fs::path path) : root_(std::move(path)) {
    dimensions_ = find_dimensions(root_);
    if (dimensions_.empty()) dimensions_.push_back("overworld");
    if (std::find(dimensions_.begin(), dimensions_.end(), dimension_) == dimensions_.end())
        dimension_ = dimensions_.front();
    region_root_ = dimension_region_dir(root_, dimension_);

    auto lvl = root_ / "level.dat";
    if (fs::exists(lvl)) {
        std::ifstream f(lvl, std::ios::binary | std::ios::ate);
        std::vector<u8> buf(static_cast<size_t>(f.tellg()));
        f.seekg(0);
        f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        try {
            auto doc = nbt::parse_auto(buf);
            if (const auto* t = doc.root.find("Data.LevelName"))
                if (auto* s = t->as_string()) info_.name = *s;
            if (const auto* t = doc.root.find("Data.DataVersion"))
                info_.data_version = static_cast<i32>(t->as_int());
            if (const auto* t = doc.root.find("Data.Version.Name"))
                if (auto* s = t->as_string()) info_.version_name = *s;
            if (const auto* t = doc.root.find("Data.SpawnX")) info_.spawn_x = (i32)t->as_int();
            if (const auto* t = doc.root.find("Data.SpawnY")) info_.spawn_y = (i32)t->as_int();
            if (const auto* t = doc.root.find("Data.SpawnZ")) info_.spawn_z = (i32)t->as_int();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "dine: level.dat parse failed (%s): %s\n", lvl.string().c_str(), e.what());
        }
    }
    if (info_.name.empty()) info_.name = vw::plat::u8str(root_.filename());
}

bool World::set_dimension(const std::string& name) {
    if (std::find(dimensions_.begin(), dimensions_.end(), name) == dimensions_.end()) return false;
    if (name == dimension_) return true;
    std::lock_guard lk(regions_mtx_);
    dimension_ = name;
    region_root_ = dimension_region_dir(root_, dimension_);
    regions_.clear();
    return true;
}

std::vector<std::pair<i32, i32>> World::available_chunks() const {
    std::vector<std::pair<i32, i32>> out;
    const auto& dir = region_root_;
    if (!fs::exists(dir)) return out;
    for (auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() != ".mca") continue;
        try {
            RegionFile rf(e.path());
            for (int lz = 0; lz < 32; ++lz)
                for (int lx = 0; lx < 32; ++lx)
                    if (rf.has_chunk(lx, lz))
                        out.emplace_back(rf.region_x() * 32 + lx, rf.region_z() * 32 + lz);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "dine: region %s failed: %s\n", e.path().string().c_str(), ex.what());
        }
    }
    return out;
}

RegionFile* World::region_for_locked(i32 cx, i32 cz) {
    // Вызывается ТОЛЬКО под regions_mtx_: возвращаемый указатель валиден,
    // пока поток удерживает мьютекс (set_dimension() очищает regions_
    // под тем же мьютексом, поэтому UAF исключён).
    const i32 rx = cx >> 5, rz = cz >> 5;
    const u64 key = region_key(rx, rz);
    if (auto it = regions_.find(key); it != regions_.end()) return it->second.get();
    auto p = region_root_ / ("r." + std::to_string(rx) + "." + std::to_string(rz) + ".mca");
    if (!fs::exists(p)) {
        regions_.emplace(key, nullptr);
        return nullptr;
    }
    auto rf = std::make_unique<RegionFile>(p);
    auto* raw = rf.get();
    regions_.emplace(key, std::move(rf));
    return raw;
}

std::optional<Chunk> World::load_chunk(i32 cx, i32 cz) {
    std::optional<nbt::Document> doc;
    {
        // Читаем чанк под тем же мьютексом, под которым set_dimension()
        // очищает кэш регионов: сырая ссылка на RegionFile не может
        // протухнуть на середине read_chunk().
        std::lock_guard lk(regions_mtx_);
        RegionFile* rf = region_for_locked(cx, cz);
        if (!rf) return std::nullopt;
        doc = rf->read_chunk(cx & 31, cz & 31);
    }
    if (!doc) return std::nullopt;
    return decode_chunk(*doc, cx, cz);
}

std::vector<Chunk> World::load_area(ThreadPool& pool, i32 cx0, i32 cz0, i32 cx1, i32 cz1) {
    const i32 w = cx1 - cx0 + 1, h = cz1 - cz0 + 1;
    std::vector<std::optional<Chunk>> tmp(static_cast<size_t>(w) * h);
    std::atomic<size_t> fail_count{0};
    pool.parallel_for(0, tmp.size(), [&](size_t i) {
        const i32 cx = cx0 + static_cast<i32>(i) % w;
        const i32 cz = cz0 + static_cast<i32>(i) / w;
        try { tmp[i] = load_chunk(cx, cz); } catch (const std::exception& ex) {
            fail_count.fetch_add(1, std::memory_order_relaxed);
            std::fprintf(stderr, "dine: chunk %d,%d failed: %s\n", cx, cz, ex.what());
        }
    });
    if (fail_count.load() > 0) {
        std::fprintf(stderr, "dine: %zu chunks failed to load in area [%d,%d]..[%d,%d]\n",
                     fail_count.load(), cx0, cz0, cx1, cz1);
    }
    std::vector<Chunk> out;
    for (auto& c : tmp)
        if (c) out.push_back(std::move(*c));
    return out;
}

u8 World::biome_intern(const std::string& name) {
    std::lock_guard lk(biome_mtx_);
    if (biome_names_.empty()) biome_names_.push_back("minecraft:plains");
    for (size_t i = 0; i < biome_names_.size(); ++i)
        if (biome_names_[i] == name) return static_cast<u8>(i);
    if (biome_names_.size() >= 255) return 0;
    biome_names_.push_back(name);
    return static_cast<u8>(biome_names_.size() - 1);
}

std::optional<Chunk> World::decode_chunk(const nbt::Document& doc, i32 cx, i32 cz) {
    Chunk ch;
    ch.cx = cx;
    ch.cz = cz;
    if (const auto* dv = doc.root.find("DataVersion"))
        ch.data_version = static_cast<i32>(dv->as_int());

    auto intern = [this](const BlockState& bs) {
        return palette_.intern(bs);
    };

    auto fill_legacy_biomes = [&](const nbt::Document& d2, Chunk& c2) {
        const auto* bi = d2.root.find("Level.Biomes");
        if (!bi) return;
        const auto* arr = bi->as<std::vector<i32>>();
        if (!arr || arr->size() < 256) return;
        const bool is3d = arr->size() >= 1024;
        for (auto& sec : c2.sections) {
            sec.biomes.resize(64);
            for (int qy = 0; qy < 4; ++qy)
                for (int qz = 0; qz < 4; ++qz)
                    for (int qx = 0; qx < 4; ++qx) {
                        int id = 0;
                        if (is3d) {
                            const i64 index = (static_cast<i64>(sec.y) * 4 + qy) * 16 +
                                              qz * 4 + qx;
                            if (index < 0 || static_cast<size_t>(index) >= arr->size())
                                continue;
                            id = (*arr)[static_cast<size_t>(index)];
                        } else {
                            id = (*arr)[static_cast<size_t>(((qz * 4 + 1) << 4) |
                                                            (qx * 4 + 1))];
                        }
                        sec.biomes[(qy << 4) | (qz << 2) | qx] =
                            biome_intern(std::string(legacy_biome_name(id)));
                    }
        }
    };

    if (ch.data_version >= 2844) {
        const auto* secs = doc.root.find("sections");
        if (!secs || !secs->as_list()) return ch;
        auto read_biomes_118 = [&](const nbt::Compound& sc, Section& sec) {
            auto bIt = sc.find("biomes");
            if (bIt == sc.end()) return;
            const auto* bC = bIt->second.as_compound();
            if (!bC) return;
            std::vector<std::string> bp;
            if (auto p = bC->find("palette"); p != bC->end() && p->second.as_list())
                for (const auto& it : p->second.as_list()->items)
                    if (const std::string* s = it.as_string()) bp.push_back(*s);
            if (bp.empty()) return;
            std::vector<u8> gid;
            gid.reserve(bp.size());
            for (auto& b : bp) gid.push_back(biome_intern(b));
            if (auto d = bC->find("data"); d != bC->end() && bp.size() > 1)
                if (const auto* la = d->second.as<std::vector<i64>>(); la) {
                    const u32 bits = std::max<u32>(1, std::bit_width(bp.size() - 1));
                    auto idx = unpack_indices(*la, bits, 64, true);
                    sec.biomes.resize(64);
                    for (size_t i = 0; i < 64; ++i)
                        sec.biomes[i] = gid[idx[i] < gid.size() ? idx[i] : 0];
                    return;
                }
            sec.biomes.assign(64, gid[0]);
        };
        for (const auto& st : secs->as_list()->items) {
            const auto* sc = st.as_compound();
            if (!sc) continue;
            Section sec;
            if (auto it = sc->find("Y"); it != sc->end())
                sec.y = static_cast<i32>(it->second.as_int());
            read_biomes_118(*sc, sec);
            auto bsIt = sc->find("block_states");
            if (bsIt == sc->end()) continue;
            const auto* bsC = bsIt->second.as_compound();
            if (!bsC) continue;

            std::vector<BlockState> pal;
            if (auto p = bsC->find("palette"); p != bsC->end() && p->second.as_list())
                pal = read_palette(*p->second.as_list());
            if (pal.empty()) continue;

            std::vector<u16> gids(pal.size());
            bool all_air = true;
            for (size_t i = 0; i < pal.size(); ++i) {
                gids[i] = intern(pal[i]);
                if (!pal[i].is_air()) all_air = false;
            }
            auto d = bsC->find("data");
            if (d == bsC->end() || pal.size() == 1) {
                if (all_air) continue;
                sec.uniform = gids[0];
                ch.sections.push_back(std::move(sec));
                continue;
            }
            const auto* la = d->second.as<std::vector<i64>>();
            if (!la) continue;
            const u32 bits = std::max<u32>(4, std::bit_width(pal.size() - 1));
            auto idx = unpack_indices(*la, bits, 4096,  true);
            sec.blocks.resize(4096);
            for (size_t i = 0; i < 4096; ++i)
                sec.blocks[i] = gids[idx[i] < gids.size() ? idx[i] : 0];
            ch.sections.push_back(std::move(sec));
        }
        return ch;
    }

    if (ch.data_version >= 1451) {
        const auto* secs = doc.root.find("Level.Sections");
        if (!secs || !secs->as_list()) return ch;
        const bool padded = ch.data_version >= 2529;
        for (const auto& st : secs->as_list()->items) {
            const auto* sc = st.as_compound();
            if (!sc) continue;
            Section sec;
            if (auto it = sc->find("Y"); it != sc->end())
                sec.y = static_cast<i32>(it->second.as_int());
            auto pIt = sc->find("Palette");
            auto dIt = sc->find("BlockStates");
            if (pIt == sc->end() || dIt == sc->end()) continue;
            const auto* plist = pIt->second.as_list();
            const auto* la = dIt->second.as<std::vector<i64>>();
            if (!plist || !la) continue;
            auto pal = read_palette(*plist);
            if (pal.empty()) continue;

            std::vector<u16> gids(pal.size());
            for (size_t i = 0; i < pal.size(); ++i) gids[i] = intern(pal[i]);
            const u32 bits = std::max<u32>(4, std::bit_width(pal.size() - 1));
            auto idx = unpack_indices(*la, bits, 4096, padded);
            sec.blocks.resize(4096);
            for (size_t i = 0; i < 4096; ++i)
                sec.blocks[i] = gids[idx[i] < gids.size() ? idx[i] : 0];
            ch.sections.push_back(std::move(sec));
        }
        fill_legacy_biomes(doc, ch);
        return ch;
    }

    {
        const auto* secs = doc.root.find("Level.Sections");
        if (!secs || !secs->as_list()) return ch;

        std::unordered_map<u32, u16> cache;
        for (const auto& st : secs->as_list()->items) {
            const auto* sc = st.as_compound();
            if (!sc) continue;
            Section sec;
            if (auto it = sc->find("Y"); it != sc->end())
                sec.y = static_cast<i32>(it->second.as_int());
            auto bIt = sc->find("Blocks");
            if (bIt == sc->end()) continue;
            const auto* blocks = bIt->second.as<std::vector<i8>>();
            if (!blocks || blocks->size() < 4096) continue;
            const std::vector<i8>* add = nullptr;
            const std::vector<i8>* data = nullptr;
            if (auto it = sc->find("Add"); it != sc->end())
                add = it->second.as<std::vector<i8>>();
            if (auto it = sc->find("Data"); it != sc->end())
                data = it->second.as<std::vector<i8>>();

            sec.blocks.resize(4096);
            for (size_t i = 0; i < 4096; ++i) {
                int id = static_cast<u8>((*blocks)[i]);
                if (add && add->size() >= 2048) id |= nibble(*add, i) << 8;
                const int meta = (data && data->size() >= 2048) ? nibble(*data, i) : 0;
                const u32 key = static_cast<u32>(id) << 4 | static_cast<u32>(meta);
                auto cit = cache.find(key);
                u16 gid;
                if (cit != cache.end()) gid = cit->second;
                else { gid = intern(legacy_block(id, meta)); cache.emplace(key, gid); }
                sec.blocks[i] = gid;
            }
            ch.sections.push_back(std::move(sec));
        }
        fill_legacy_biomes(doc, ch);
        return ch;
    }
}

}
