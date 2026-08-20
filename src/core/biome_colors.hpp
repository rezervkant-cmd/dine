#pragma once
#include "types.hpp"
#include <array>
#include <string_view>
#include <tuple>

namespace vw {

struct BiomeRGB { u8 r = 0, g = 0, b = 0; };

struct BiomeColors {
    BiomeRGB grass;
    BiomeRGB foliage;
    BiomeRGB water;
};

inline std::array<f32, 2> biome_temp_rain(std::string_view name) {
    std::string_view n = name;
    if (size_t c = n.find(':'); c != std::string_view::npos) n.remove_prefix(c + 1);
    struct E { std::string_view k; f32 t, d; };
    static constexpr E tbl[] = {
        {"plains", 0.8f, 0.4f},          {"sunflower_plains", 0.8f, 0.4f},
        {"desert", 2.0f, 0.0f},          {"forest", 0.7f, 0.8f},
        {"flower_forest", 0.7f, 0.8f},   {"birch_forest", 0.6f, 0.6f},
        {"dark_forest", 0.7f, 0.8f},     {"pale_garden", 0.7f, 0.8f},
        {"taiga", 0.25f, 0.8f},          {"old_growth_pine_taiga", 0.3f, 0.8f},
        {"old_growth_spruce_taiga", 0.25f, 0.8f},
        {"snowy_plains", 0.0f, 0.5f},    {"ice_spikes", 0.0f, 0.5f},
        {"snowy_taiga", -0.5f, 0.4f},    {"snowy_slopes", -0.3f, 0.9f},
        {"frozen_peaks", -0.7f, 0.9f},   {"jagged_peaks", -0.7f, 0.9f},
        {"grove", -0.2f, 0.8f},          {"swamp", 0.8f, 0.9f},
        {"mangrove_swamp", 0.8f, 0.9f},  {"jungle", 0.95f, 0.9f},
        {"sparse_jungle", 0.95f, 0.8f},  {"bamboo_jungle", 0.95f, 0.9f},
        {"savanna", 1.2f, 0.0f},         {"savanna_plateau", 1.0f, 0.0f},
        {"windswept_savanna", 1.1f, 0.0f},
        {"badlands", 2.0f, 0.0f},        {"wooded_badlands", 2.0f, 0.0f},
        {"eroded_badlands", 2.0f, 0.0f}, {"meadow", 0.5f, 0.8f},
        {"cherry_grove", 0.5f, 0.8f},    {"stony_peaks", 1.0f, 0.3f},
        {"windswept_hills", 0.2f, 0.3f}, {"windswept_forest", 0.2f, 0.3f},
        {"windswept_gravelly_hills", 0.2f, 0.3f},
        {"river", 0.5f, 0.5f},           {"beach", 0.8f, 0.4f},
        {"snowy_beach", 0.05f, 0.3f},    {"stony_shore", 0.2f, 0.3f},
        {"lush_caves", 0.5f, 0.5f},      {"dripstone_caves", 0.8f, 0.4f},
        {"deep_dark", 0.8f, 0.4f},       {"the_void", 0.5f, 0.5f},
    };
    for (const auto& e : tbl)
        if (n == e.k) return {e.t, e.d};
    if (n.find("ocean") != std::string_view::npos ||
        n.find("river") != std::string_view::npos)
        return {0.5f, 0.5f};
    return {0.8f, 0.4f};
}

inline BiomeRGB biome_water_rgb(std::string_view name) {
    std::string_view n = name;
    if (size_t c = n.find(':'); c != std::string_view::npos) n.remove_prefix(c + 1);
    if (n.find("swamp") != std::string_view::npos) return {0x61, 0x7B, 0x64};
    if (n.find("warm_ocean") != std::string_view::npos ||
        n == "warm_deep_ocean" || n == "deep_warm_ocean")
        return {0x43, 0xD5, 0xEE};
    if (n.find("lukewarm") != std::string_view::npos) return {0x45, 0xAD, 0xF2};
    if (n.find("cold") != std::string_view::npos) return {0x3D, 0x57, 0xD6};
    if (n.find("frozen") != std::string_view::npos) return {0x39, 0x38, 0xC9};
    return {0x3F, 0x76, 0xE4};
}

inline std::array<f32, 2> biome_colormap_xy(f32 t, f32 d) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    if (d < 0) d = 0;
    if (d > 1) d = 1;
    d *= t;
    return {1.0f - t, 1.0f - d};
}

inline std::string_view legacy_biome_name(int id) {
    static constexpr std::string_view tbl[] = {
        "minecraft:ocean", "minecraft:plains", "minecraft:desert",
        "minecraft:windswept_hills", "minecraft:forest", "minecraft:taiga",
        "minecraft:swamp", "minecraft:river", "minecraft:nether_wastes",
        "minecraft:the_end", "minecraft:frozen_ocean", "minecraft:frozen_river",
        "minecraft:snowy_plains", "minecraft:snowy_plains",
        "minecraft:mushroom_fields", "minecraft:mushroom_fields",
        "minecraft:beach", "minecraft:windswept_hills", "minecraft:forest",
        "minecraft:taiga", "minecraft:windswept_hills", "minecraft:jungle",
        "minecraft:jungle", "minecraft:sparse_jungle", "minecraft:deep_ocean",
        "minecraft:stony_shore", "minecraft:snowy_beach",
        "minecraft:birch_forest", "minecraft:birch_forest",
        "minecraft:dark_forest", "minecraft:snowy_taiga", "minecraft:snowy_taiga",
        "minecraft:old_growth_pine_taiga", "minecraft:old_growth_pine_taiga",
        "minecraft:windswept_hills", "minecraft:savanna", "minecraft:savanna_plateau",
        "minecraft:badlands", "minecraft:wooded_badlands", "minecraft:badlands",
        "minecraft:the_end", "minecraft:the_end", "minecraft:the_end",
        "minecraft:the_end", "minecraft:warm_ocean", "minecraft:lukewarm_ocean",
        "minecraft:cold_ocean", "minecraft:warm_ocean", "minecraft:lukewarm_ocean",
        "minecraft:cold_ocean", "minecraft:frozen_ocean",
    };
    if (id >= 0 && id < static_cast<int>(std::size(tbl))) return tbl[id];
    switch (id) {
        case 127: return "minecraft:the_void";
        case 129: return "minecraft:sunflower_plains";
        case 130: return "minecraft:desert";
        case 131: return "minecraft:windswept_gravelly_hills";
        case 132: return "minecraft:flower_forest";
        case 133: return "minecraft:taiga";
        case 134: return "minecraft:swamp";
        case 140: return "minecraft:ice_spikes";
        case 149: return "minecraft:jungle";
        case 151: return "minecraft:sparse_jungle";
        case 155: return "minecraft:birch_forest";
        case 156: return "minecraft:birch_forest";
        case 157: return "minecraft:dark_forest";
        case 158: return "minecraft:snowy_taiga";
        case 160: return "minecraft:old_growth_spruce_taiga";
        case 161: return "minecraft:old_growth_spruce_taiga";
        case 162: return "minecraft:windswept_gravelly_hills";
        case 163: return "minecraft:windswept_savanna";
        case 164: return "minecraft:savanna_plateau";
        case 165: return "minecraft:eroded_badlands";
        case 166: return "minecraft:wooded_badlands";
        case 167: return "minecraft:badlands";
        case 168: return "minecraft:bamboo_jungle";
        case 169: return "minecraft:bamboo_jungle";
        case 170: return "minecraft:soul_sand_valley";
        case 171: return "minecraft:crimson_forest";
        case 172: return "minecraft:warped_forest";
        case 173: return "minecraft:basalt_deltas";
        case 174: return "minecraft:dripstone_caves";
        case 175: return "minecraft:lush_caves";
        default: return "minecraft:plains";
    }
}

}
