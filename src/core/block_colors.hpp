#pragma once
#include "types.hpp"
#include <array>
#include <string_view>

namespace vw {

inline std::array<u8, 4> fallback_block_color(std::string_view full_name) {

    std::string_view n = full_name;
    if (auto c = n.find(':'); c != std::string_view::npos) n.remove_prefix(c + 1);

    struct E { std::string_view k; u8 r, g, b; };

    static constexpr E exact[] = {
        {"grass_block", 110, 156, 66},  {"dirt", 134, 96, 67},
        {"stone", 125, 125, 125},       {"cobblestone", 110, 110, 110},
        {"sand", 219, 207, 163},        {"red_sand", 189, 106, 55},
        {"gravel", 126, 124, 122},      {"water", 47, 89, 173},
        {"lava", 207, 91, 19},          {"bedrock", 60, 60, 60},
        {"snow", 240, 246, 246},        {"snow_block", 242, 248, 248},
        {"barrier", 219, 33, 26},
        {"ice", 145, 183, 253},         {"clay", 158, 164, 176},
        {"netherrack", 97, 38, 38},     {"glowstone", 248, 212, 111},
        {"obsidian", 21, 18, 30},       {"end_stone", 221, 223, 165},
        {"glass", 200, 230, 240},       {"bricks", 150, 97, 83},
        {"mycelium", 111, 99, 105},     {"podzol", 90, 63, 26},
        {"pumpkin", 227, 144, 29},      {"melon", 111, 153, 31},
        {"tnt", 200, 60, 40},           {"sponge", 195, 192, 74},
        {"gold_block", 249, 236, 78},   {"iron_block", 220, 220, 220},
        {"diamond_block", 98, 219, 214},{"emerald_block", 41, 199, 92},
        {"redstone_block", 171, 27, 9}, {"quartz_block", 235, 229, 222},
        {"coal_ore", 115, 115, 115},    {"iron_ore", 135, 130, 126},
        {"gold_ore", 143, 139, 124},    {"diamond_ore", 129, 140, 143},
        {"redstone_ore", 132, 107, 107},{"lapis_ore", 102, 112, 134},
        {"emerald_ore", 110, 129, 116}, {"copper_ore", 124, 125, 120},
        {"deepslate", 80, 80, 82},      {"granite", 149, 103, 85},
        {"diorite", 188, 188, 188},     {"andesite", 136, 136, 137},
        {"calcite", 223, 224, 220},     {"tuff", 108, 109, 102},
        {"moss_block", 89, 109, 45},    {"mud", 60, 57, 60},
        {"soul_sand", 81, 62, 50},      {"soul_soil", 75, 57, 46},
        {"blackstone", 42, 36, 41},     {"basalt", 73, 72, 77},
        {"crimson_nylium", 130, 31, 31},{"warped_nylium", 43, 114, 101},
        {"honey_block", 251, 185, 52},  {"slime_block", 111, 192, 91},
        {"bookshelf", 180, 144, 90},    {"crafting_table", 156, 108, 62},
        {"furnace", 110, 110, 110},     {"hay_block", 198, 168, 36},
    };
    for (const auto& e : exact)
        if (n == e.k) return {e.r, e.g, e.b, 255};

    static constexpr E subs[] = {
        {"leaves", 60, 120, 40},   {"log", 107, 83, 50},
        {"planks", 162, 130, 78},  {"wood", 107, 83, 50},
        {"stem", 120, 60, 90},     {"sapling", 80, 140, 60},
        {"wool", 222, 222, 222},   {"carpet", 222, 222, 222},
        {"terracotta", 152, 94, 67},{"concrete", 180, 180, 180},
        {"prismarine", 99, 171, 158},{"purpur", 169, 125, 169},
        {"nether_brick", 44, 22, 26},{"sandstone", 216, 203, 155},
        {"mushroom", 183, 145, 120},{"coral", 207, 91, 130},
        {"copper", 192, 107, 79},  {"amethyst", 133, 97, 191},
        {"azalea", 93, 118, 48},   {"bamboo", 122, 156, 63},
        {"grass", 110, 156, 66},   {"fern", 90, 130, 60},
        {"flower", 200, 180, 90},  {"tulip", 210, 120, 130},
        {"stone", 125, 125, 125},  {"brick", 150, 97, 83},
        {"ore", 120, 120, 120},    {"rail", 130, 110, 90},
        {"door", 150, 115, 70},    {"fence", 150, 115, 70},
        {"slab", 140, 140, 140},   {"stairs", 140, 140, 140},
        {"water", 47, 89, 173},    {"lava", 207, 91, 19},
    };
    for (const auto& e : subs)
        if (n.find(e.k) != std::string_view::npos) return {e.r, e.g, e.b, 255};

    u32 h = 2166136261u;
    for (char c : n) h = (h ^ static_cast<u8>(c)) * 16777619u;
    return {static_cast<u8>(96 + (h & 0x7F)),
            static_cast<u8>(96 + ((h >> 7) & 0x7F)),
            static_cast<u8>(96 + ((h >> 14) & 0x7F)), 255};
}

}
