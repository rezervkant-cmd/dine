#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <span>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <stdexcept>

namespace vw {

using u8 = uint8_t;  using u16 = uint16_t; using u32 = uint32_t; using u64 = uint64_t;
using i8 = int8_t;   using i16 = int16_t;  using i32 = int32_t;  using i64 = int64_t;
using f32 = float;   using f64 = double;

struct BlockState {
    std::string name;
    std::vector<std::pair<std::string, std::string>> props;

    std::string key() const {
        std::string k = name;
        for (auto& [p, v] : props) { k += '['; k += p; k += '='; k += v; k += ']'; }
        return k;
    }
    bool is_air() const {
        return name == "minecraft:air" || name == "minecraft:cave_air" ||
               name == "minecraft:void_air";
    }
    bool is_fluid() const {
        return name == "minecraft:water" || name == "minecraft:lava" ||
               name == "minecraft:flowing_water" || name == "minecraft:flowing_lava";
    }

    bool is_decoration() const {
        auto has = [&](std::string_view s) { return name.find(s) != std::string_view::npos; };
        auto is  = [&](std::string_view s) { return name == s; };
        if (is("minecraft:grass") || is("minecraft:short_grass") ||
            is("minecraft:tall_grass") || is("minecraft:fern") ||
            is("minecraft:large_fern") || is("minecraft:seagrass") ||
            is("minecraft:tall_seagrass") || is("minecraft:snow") ||
            is("minecraft:dead_bush") || is("minecraft:cobweb"))
            return true;
        if (has("mushroom_block") || has("mushroom_stem")) return false;
        return has("flower") || has("tulip") || has("orchid") || has("allium") ||
               has("daisy") || has("dandelion") || has("poppy") || has("lilac") ||
               has("rose_bush") || has("peony") || has("sapling") || has("torch") ||
               has("rail") || has("ladder") || has("vine") || has("button") ||
               has("lever") || has("sign") || has("banner") || has("pressure_plate") ||
               has("flower_pot") || has("potted_") || has("kelp") || has("sugar_cane") ||
               has("sweet_berry") || has("dripleaf") || has("_roots") ||
               has("sprouts") || has("fungus") || has("azure_bluet") ||
               has("cornflower") || has("lily_of_the_valley") || has("wither_rose") ||
               has("torchflower") || has("pink_petals") || has("_mushroom") ||
               has("redstone_wire") || has("tripwire") || has("candle") ||
               has("scaffolding") || has("chain") || has("lantern") ||
               has("carpet") || has("pitcher_plant") || has("bush");
    }
};

// Потокобезопасная глобальная палитра. Все методы блокируют внутренний мьютекс.
// Возвращает копии вместо ссылок, чтобы избежать инвалидации при реаллокации.
// ID 0 всегда закреплён за minecraft:air: сетка заполняется нулями как «пусто»,
// поэтому первый встреченный блок не должен получать нулевой ID.
class PaletteOverflow : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class GlobalPalette {
public:
    static constexpr size_t kMaxStates = 65535;  // u16-адресуемый предел
    GlobalPalette() {
        states_.push_back(BlockState{"minecraft:air", {}});
        index_.emplace("minecraft:air", 0);
    }
    // Переполнение палитры — явная ошибка: молча подменять блок воздухом
    // нельзя, иначе часть мира бесследно исчезает из экспорта.
    u16 intern(const BlockState& bs) {
        auto k = bs.key();
        std::lock_guard lk(mtx_);
        if (auto it = index_.find(k); it != index_.end()) return it->second;
        if (states_.size() >= kMaxStates) {
            overflowed_ = true;
            throw PaletteOverflow(
                "block palette overflow: more than 65534 distinct block states");
        }
        u16 id = static_cast<u16>(states_.size());
        states_.push_back(bs);
        index_.emplace(std::move(k), id);
        return id;
    }
    BlockState get(u16 id) const {
        std::lock_guard lk(mtx_);
        if (id < states_.size()) return states_[id];
        return BlockState{"minecraft:air", {}};
    }
    size_t size() const {
        std::lock_guard lk(mtx_);
        return states_.size();
    }
    std::vector<BlockState> snapshot() const {
        std::lock_guard lk(mtx_);
        return states_;
    }
    // true, если хотя бы одна попытка intern() уперлась в предел u16.
    bool overflowed() const {
        std::lock_guard lk(mtx_);
        return overflowed_;
    }
private:
    mutable std::mutex mtx_;
    bool overflowed_ = false;
    std::vector<BlockState> states_;
    std::unordered_map<std::string, u16> index_;
};

struct Section {
    i32 y = 0;
    std::vector<u16> blocks;
    u16 uniform = 0;
    std::vector<u8> biomes;

    u16 at(int x, int y_, int z) const {
        return blocks.empty() ? uniform : blocks[(y_ << 8) | (z << 4) | x];
    }
    u8 biome_at(int qx, int qy, int qz) const {
        if (biomes.size() < 64) return 0;
        return biomes[(qy << 4) | (qz << 2) | qx];
    }
};

struct Chunk {
    i32 cx = 0, cz = 0;
    i32 data_version = 0;
    std::vector<Section> sections;
    std::vector<u8> biomes;

    const Section* section_at(i32 sec_y) const {
        for (auto& s : sections) if (s.y == sec_y) return &s;
        return nullptr;
    }
};

struct BBox {
    i32 x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
    i32 sx() const { return x1 - x0 + 1; }
    i32 sy() const { return y1 - y0 + 1; }
    i32 sz() const { return z1 - z0 + 1; }
};

}
