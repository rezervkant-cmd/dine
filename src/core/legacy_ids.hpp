#pragma once
#include "types.hpp"

namespace vw {

inline BlockState legacy_block(int id, int meta) {
    auto mk = [](const char* n) { return BlockState{n, {}}; };
    switch (id) {
        case 0:  return mk("minecraft:air");
        case 1:
            switch (meta) {
                case 1: return {"minecraft:granite", {}};
                case 2: return {"minecraft:polished_granite", {}};
                case 3: return {"minecraft:diorite", {}};
                case 4: return {"minecraft:polished_diorite", {}};
                case 5: return {"minecraft:andesite", {}};
                case 6: return {"minecraft:polished_andesite", {}};
                default: return mk("minecraft:stone");
            }
        case 2:  return mk("minecraft:grass_block");
        case 3:  return meta == 2 ? mk("minecraft:podzol") : mk("minecraft:dirt");
        case 4:  return mk("minecraft:cobblestone");
        case 5: {
            static const char* p[] = {"oak", "spruce", "birch", "jungle", "acacia", "dark_oak"};
            return {std::string("minecraft:") + p[meta % 6] + "_planks", {}};
        }
        case 7:  return mk("minecraft:bedrock");
        case 8: case 9:   return mk("minecraft:water");
        case 10: case 11: return mk("minecraft:lava");
        case 12: return meta == 1 ? mk("minecraft:red_sand") : mk("minecraft:sand");
        case 13: return mk("minecraft:gravel");
        case 14: return mk("minecraft:gold_ore");
        case 15: return mk("minecraft:iron_ore");
        case 16: return mk("minecraft:coal_ore");
        case 17: {
            static const char* w[] = {"oak", "spruce", "birch", "jungle"};
            return {std::string("minecraft:") + w[meta & 3] + "_log", {}};
        }
        case 18: {
            static const char* w[] = {"oak", "spruce", "birch", "jungle"};
            return {std::string("minecraft:") + w[meta & 3] + "_leaves", {}};
        }
        case 20: return mk("minecraft:glass");
        case 21: return mk("minecraft:lapis_ore");
        case 24: return mk("minecraft:sandstone");
        case 35: {
            static const char* c[] = {"white","orange","magenta","light_blue","yellow","lime",
                "pink","gray","light_gray","cyan","purple","blue","brown","green","red","black"};
            return {std::string("minecraft:") + c[meta & 15] + "_wool", {}};
        }
        case 41: return mk("minecraft:gold_block");
        case 42: return mk("minecraft:iron_block");
        case 45: return mk("minecraft:bricks");
        case 46: return mk("minecraft:tnt");
        case 48: return mk("minecraft:mossy_cobblestone");
        case 49: return mk("minecraft:obsidian");
        case 56: return mk("minecraft:diamond_ore");
        case 57: return mk("minecraft:diamond_block");
        case 73: case 74: return mk("minecraft:redstone_ore");
        case 78: return mk("minecraft:snow");
        case 79: return mk("minecraft:ice");
        case 80: return mk("minecraft:snow_block");
        case 82: return mk("minecraft:clay");
        case 87: return mk("minecraft:netherrack");
        case 89: return mk("minecraft:glowstone");
        case 98: return mk("minecraft:stone_bricks");
        case 112: return mk("minecraft:nether_bricks");
        case 121: return mk("minecraft:end_stone");
        case 129: return mk("minecraft:emerald_ore");
        case 133: return mk("minecraft:emerald_block");
        case 152: return mk("minecraft:redstone_block");
        case 155: return mk("minecraft:quartz_block");
        case 159: {
            static const char* c[] = {"white","orange","magenta","light_blue","yellow","lime",
                "pink","gray","light_gray","cyan","purple","blue","brown","green","red","black"};
            return {std::string("minecraft:") + c[meta & 15] + "_terracotta", {}};
        }
        case 179: return mk("minecraft:red_sandstone");
        default: {

            BlockState bs{"legacy:" + std::to_string(id), {}};
            if (meta) bs.props.emplace_back("meta", std::to_string(meta));
            return bs;
        }
    }
}

}
