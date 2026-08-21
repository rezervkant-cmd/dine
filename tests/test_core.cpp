#include "anvil/world.hpp"
#include "anvil/lz4_block.hpp"
#include "anvil/region.hpp"
#include "mods/mod_assets.hpp"
#include "mods/json.hpp"
#include "mesh/grid.hpp"
#include "nbt/nbt.hpp"
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <atomic>
#include <thread>

using namespace vw;
namespace fs = std::filesystem;

namespace vw::mods::png {
std::optional<TextureRGBA> decode(std::span<const u8> file);
}

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
    else std::printf("ok   %s\n", #cond); \
} while (0)

int main(int argc, char** argv) {
    fs::path data = argc > 1 ? argv[1] : "tests/data";
    ThreadPool pool;

    anvil::World modern(data / "world_modern");
    CHECK(modern.info().name == "TestModern");
    CHECK(modern.info().data_version == 3465);
    CHECK(std::find(modern.dimensions().begin(), modern.dimensions().end(), "overworld") != modern.dimensions().end());
    CHECK(std::find(modern.dimensions().begin(), modern.dimensions().end(), "the_nether") != modern.dimensions().end());
    CHECK(std::find(modern.dimensions().begin(), modern.dimensions().end(), "testmod/moon") != modern.dimensions().end());
    CHECK(modern.set_dimension("the_nether"));
    CHECK(modern.dimension() == "the_nether");
    CHECK(modern.load_chunk(0, 0).has_value());
    CHECK(modern.set_dimension("overworld"));

    auto ch = modern.load_chunk(0, 0);
    CHECK(ch.has_value());
    CHECK(!ch->sections.empty());

    const auto& pal = modern.palette();
    auto name_at = [&](const Chunk& c, int x, int y, int z) {
        const auto* s = c.section_at(y >> 4);
        return s ? pal.get(s->at(x, y & 15, z)).name : std::string("minecraft:air");
    };
    CHECK(name_at(*ch, 0, 0, 0) == "minecraft:stone");
    CHECK(name_at(*ch, 0, 2, 0) == "minecraft:grass_block");
    CHECK(name_at(*ch, 0, 3, 0) == "minecraft:water");
    CHECK(name_at(*ch, 1, 3, 1) == "testmod:ruby_block");
    CHECK(name_at(*ch, 5, 10, 5) == "minecraft:air");

    // Minecraft's type-4 chunk payload uses lz4-java LZ4Block, not LZ4 Frame.
    anvil::World lz4(data / "world_lz4");
    CHECK(lz4.info().name == "TestLz4");
    auto lz4ch = lz4.load_chunk(0, 0);
    CHECK(lz4ch.has_value());
    if (lz4ch) {
        const auto* s = lz4ch->section_at(0);
        CHECK(s != nullptr);
        if (s) CHECK(lz4.palette().get(s->at(1, 3, 1)).name == "testmod:ruby_block");
    }

    anvil::World legacy(data / "world_legacy");
    auto lch = legacy.load_chunk(0, 0);
    CHECK(lch.has_value());
    const auto& lpal = legacy.palette();
    auto lname = [&](int x, int y, int z) {
        const auto* s = lch->section_at(y >> 4);
        return s ? lpal.get(s->at(x, y & 15, z)).name : std::string("minecraft:air");
    };
    CHECK(lname(0, 0, 0) == "minecraft:stone");
    CHECK(lname(0, 2, 0) == "minecraft:grass_block");
    CHECK(lname(5, 3, 0) == "minecraft:red_wool");

    mods::AssetRegistry assets;
    assets.scan_mods_dir(data / "mods", pool);
    {
        auto mods = assets.mods();
        CHECK(mods.size() == 2);
        const mods::ModInfo* tm = nullptr;
        for (const auto& mi : mods)
            if (mi.file_name.find("testmod") != std::string::npos) tm = &mi;
        CHECK(tm && tm->loader == "fabric");
    }

    auto tex_ref = assets.resolve_block_texture("testmod:ruby_block");
    CHECK(tex_ref.has_value());
    CHECK(*tex_ref == "testmod:block/ruby_block");
    auto tex = assets.load_texture(*tex_ref);
    CHECK(tex.has_value());
    CHECK(tex->w == 16 && tex->h == 16);
    CHECK(tex->pixels[0] == 0xE0 && tex->pixels[1] == 0x11 && tex->pixels[2] == 0x5F);
    auto avg = assets.average_color("testmod:ruby_block");
    CHECK(avg.has_value());

    BBox box{0, 0, 0, 15, 15, 15};
    auto chunks = modern.load_area(pool, 0, 0, 0, 0);
    CHECK(chunks.size() == 1);
    auto grid = mesh::make_grid(chunks, box);
    CHECK(grid.at(0, 0, 0) != 0);

    // Opening another world must be able to drop all old jar references.
    assets.clear();
    CHECK(assets.mods().empty());
    CHECK(!assets.has_texture("testmod:block/ruby_block"));

    // --- Регрессионные тесты исправленных дефектов ---

    // [P1] ID 0 всегда minecraft:air, даже если первым зарегистрирован камень.
    {
        GlobalPalette pal;
        CHECK(pal.intern(BlockState{"minecraft:stone", {}}) != 0);
        CHECK(pal.intern(BlockState{"minecraft:air", {}}) == 0);
        CHECK(pal.get(0).name == "minecraft:air");
    }

    // [P1] Crafted LZ4Block-заголовок с decompressed_size = 0xffffffff
    // должен дать исключение, а не попытку выделить ~4 ГБ.
    {
        std::vector<u8> evil(21 + 4, 0);
        std::memcpy(evil.data(), "LZ4Block", 8);
        evil[8] = 0x10;  // raw
        auto put32 = [&](size_t at, u32 v) {
            evil[at]     = static_cast<u8>(v);
            evil[at + 1] = static_cast<u8>(v >> 8);
            evil[at + 2] = static_cast<u8>(v >> 16);
            evil[at + 3] = static_cast<u8>(v >> 24);
        };
        put32(9, 4);              // compressed_size
        put32(13, 0xffffffffU);   // decompressed_size — атака
        bool thrown = false;
        try { (void)anvil::decode_lz4_block_stream(evil); }
        catch (const std::exception&) { thrown = true; }
        CHECK(thrown);
    }

    // [P2] Некорректное имя region-файла -> понятная ошибка, а не UB.
    {
        bool thrown = false;
        try {
            anvil::RegionFile rf(data / "world_modern" / "region" / "rabc.mca");
            (void)rf;
        } catch (const std::exception&) { thrown = true; }
        CHECK(thrown);
    }

    // [P2] JSON: хвост после документа и чрезмерная вложенность отклоняются,
    // валидный документ по-прежнему парсится.
    {
        bool tail_thrown = false;
        try { (void)json::parse("{\"x\":1} garbage"); }
        catch (const std::exception&) { tail_thrown = true; }
        CHECK(tail_thrown);

        std::string deep(300, '[');
        deep.append(300, ']');
        bool depth_thrown = false;
        try { (void)json::parse(deep); }
        catch (const std::exception&) { depth_thrown = true; }
        CHECK(depth_thrown);

        auto ok = json::parse("{\"a\": [1, 2, {\"b\": true}]}");
        CHECK(ok.obj() != nullptr);

        bool partial_number_thrown = false;
        try { (void)json::parse("[1+2]"); }
        catch (const std::exception&) { partial_number_thrown = true; }
        CHECK(partial_number_thrown);
    }

    // [P1] A short IHDR must not borrow bytes from following chunks.
    {
        auto put_be32 = [](std::vector<u8>& out, u32 v) {
            out.push_back(static_cast<u8>(v >> 24));
            out.push_back(static_cast<u8>(v >> 16));
            out.push_back(static_cast<u8>(v >> 8));
            out.push_back(static_cast<u8>(v));
        };
        auto put_chunk = [&](std::vector<u8>& out, const char type[4],
                             std::span<const u8> payload) {
            put_be32(out, static_cast<u32>(payload.size()));
            out.insert(out.end(), type, type + 4);
            out.insert(out.end(), payload.begin(), payload.end());
            put_be32(out, 0); // The fallback decoder does not validate CRCs.
        };

        const std::array<u8, 8> signature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
        std::vector<u8> malformed(signature.begin(), signature.end());
        put_be32(malformed, 0);
        malformed.insert(malformed.end(), {'I', 'H', 'D', 'R'});
        put_be32(malformed, 1); // IHDR CRC bytes are incorrectly read as width.

        // This fake following chunk supplies the remaining 9 IHDR bytes to the
        // buggy decoder: height=1, depth=8, RGBA, no interlace.
        put_be32(malformed, 1);
        malformed.insert(malformed.end(), {8, 6, 0, 0});
        malformed.push_back(0);
        put_be32(malformed, 0);

        std::array<u8, 5> scanline{0, 0x10, 0x20, 0x30, 0xff};
        uLongf compressed_size = compressBound(static_cast<uLong>(scanline.size()));
        std::vector<u8> compressed(compressed_size);
        CHECK(compress2(compressed.data(), &compressed_size, scanline.data(),
                        static_cast<uLong>(scanline.size()), 9) == Z_OK);
        compressed.resize(compressed_size);
        put_chunk(malformed, "IDAT", compressed);
        put_chunk(malformed, "IEND", {});

        CHECK(!mods::png::decode(malformed).has_value());
    }

    // [P1] A section outside the legacy 3D biome array must keep defaults.
    {
        nbt::Compound section;
        section["Y"].v = i8{16}; // First biome index is 1024, one past this array.
        section["Blocks"].v = std::vector<i8>(4096, 0);

        nbt::List sections;
        sections.elem = nbt::TagType::Compound;
        nbt::Tag section_tag;
        section_tag.v = std::move(section);
        sections.items.push_back(std::move(section_tag));

        nbt::Compound level;
        level["Sections"].v = std::move(sections);
        level["Biomes"].v = std::vector<i32>(1024, 1);

        nbt::Compound root;
        root["DataVersion"].v = i32{0};
        root["Level"].v = std::move(level);
        nbt::Document malformed_chunk;
        malformed_chunk.root.v = std::move(root);

        auto decoded = legacy.decode_chunk(malformed_chunk, 0, 0);
        CHECK(decoded.has_value());
        CHECK(decoded && decoded->sections.size() == 1);
        CHECK(decoded && decoded->sections[0].biomes == std::vector<u8>(64, 0));
    }

    // [P1] Переполнение палитры — явная ошибка PaletteOverflow, а не тихая
    // подмена блока воздухом.
    {
        GlobalPalette pal;
        for (size_t i = pal.size(); i < GlobalPalette::kMaxStates; ++i)
            (void)pal.intern(BlockState{"vw:filler_" + std::to_string(i), {}});
        CHECK(pal.size() == GlobalPalette::kMaxStates);
        CHECK(!pal.overflowed());
        bool overflow_thrown = false;
        try { (void)pal.intern(BlockState{"vw:one_too_many", {}}); }
        catch (const PaletteOverflow&) { overflow_thrown = true; }
        CHECK(overflow_thrown);
        CHECK(pal.overflowed());
        // Уже известные состояния продолжают резолвиться.
        CHECK(pal.intern(BlockState{"minecraft:air", {}}) == 0);
    }

    // [P2] Обрезанный deflate-поток обязан кидать исключение, а не молча
    // возвращать частично распакованные данные.
    {
        const std::string text(4096, 'A');
        auto src = reinterpret_cast<const u8*>(text.data());
        std::vector<u8> raw(text.begin(), text.end());
        (void)src;
        uLongf clen = compressBound(static_cast<uLong>(raw.size()));
        std::vector<u8> comp(clen);
        CHECK(compress2(comp.data(), &clen, raw.data(),
                        static_cast<uLong>(raw.size()), 9) == Z_OK);
        comp.resize(clen);
        auto full = nbt::inflate(std::span<const u8>(comp), false);
        CHECK(full.size() == raw.size());

        std::vector<u8> cut(comp.begin(), comp.begin() + comp.size() / 2);
        bool cut_thrown = false;
        try { (void)nbt::inflate(std::span<const u8>(cut), false); }
        catch (const std::exception&) { cut_thrown = true; }
        CHECK(cut_thrown);
    }

    // [P1] Гонка «чтение ассетов || clear()»: раньше load_json_asset()
    // отпускала мьютекс до обращения к ZipFile, и параллельный clear()
    // давал use-after-free. Тест шумит по реестру из нескольких потоков,
    // пока главный поток его пересобирает (под ASan ловит регрессию).
    {
        mods::AssetRegistry reg;
        ThreadPool tp;
        reg.scan_mods_dir(data / "mods", tp);
        std::atomic<bool> stop{false};
        std::atomic<size_t> reads{0};
        std::vector<std::thread> readers;
        for (int t = 0; t < 4; ++t)
            readers.emplace_back([&] {
                while (!stop.load(std::memory_order_relaxed)) {
                    (void)reg.resolve_block_texture("testmod:ruby_block");
                    (void)reg.load_model_json("testmod:block/ruby_block");
                    (void)reg.load_texture("testmod:block/ruby_block");
                    (void)reg.find_deco_entry("decomini:chair");
                    (void)reg.mods_count();
                    reads.fetch_add(1, std::memory_order_relaxed);
                }
            });
        for (int i = 0; i < 20; ++i) {
            reg.clear();
            reg.scan_mods_dir(data / "mods", tp);
        }
        stop.store(true);
        for (auto& th : readers) th.join();
        CHECK(reads.load() > 0);
        CHECK(reg.mods_count() > 0);
    }

    std::printf("\n%s (%d failures)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures);
    return failures ? 1 : 0;
}
