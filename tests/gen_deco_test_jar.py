#!/usr/bin/env python3
"""Собирает tests/data/mods/decomini.jar — мини-мод в стиле DecoCraft/ DecoNature
для теста bbmodel-поддержки (запускать вручную при изменении, jar коммитим).

decotest:deco_lamp — loader "deco:bbmodel": 32x32 resolution, material-текстура,
   поворот элемента (z 45°), поворот группы (y 45°), скрытый cube (visibility:false),
   локатор root_node; варианты facing=y.
decotest:block/deco_strip — 16x64 полоса + .png.mcmeta с "animation" (кадры).
decotest:block/deco_tall — 16x32 статичная высокая текстура БЕЗ mcmeta.
natmin:nat_bush — модель с пустыми elements + реестр decocraft.json/items.json
   (как у DecoCraft Nature: decoref -> bbmodel + material).
"""
import json, struct, zlib, zipfile, os, sys

def png(w, h, gen):
    """gen(x,y) -> (r,g,b,a)"""
    def chunk(t, data):
        c = t + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    raw = b""
    for y in range(h):
        raw += b"\x00" + b"".join(struct.pack("4B", *gen(x, y)) for x in range(w))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) +
            chunk(b"IEND", b""))

def full_cube_faces(u0, v0, u1, v1):
    return {d: {"uv": [u0, v0, u1, v1], "texture": 0}
            for d in ("north", "east", "south", "west", "up", "down")}

# --- bbmodel: лампа с поворотами -------------------------------------------
lamp = {
    "meta": {"model_format": "bedrock"},
    "name": "lamp",
    "resolution": {"width": 32, "height": 32},
    "elements": [
        {"name": "base", "uuid": "b1", "type": "cube",
         "from": [-4, 0, -4], "to": [4, 4, 4], "origin": [0, 0, 0],
         "rotation": [0, 0, 0], "faces": full_cube_faces(0, 0, 32, 32)},
        {"name": "arm", "uuid": "b2", "type": "cube",          # в группе g1 (rot y 45)
         "from": [2, 4, -2], "to": [6, 12, 2], "origin": [0, 8, 0],
         "rotation": [0, 0, 0], "faces": full_cube_faces(0, 0, 32, 32)},
        {"name": "wing", "uuid": "b3", "type": "cube",         # поворот элемента z 45
         "from": [-6, 4, -2], "to": [-2, 8, 2], "origin": [-4, 6, 0],
         "rotation": [0, 0, 45], "faces": full_cube_faces(0, 0, 32, 32)},
        {"name": "ghost", "uuid": "b4", "type": "cube",        # НЕ должен выпечься
         "visibility": False,
         "from": [0, 16, 0], "to": [4, 20, 4], "origin": [0, 0, 0],
         "faces": full_cube_faces(0, 0, 32, 32)},
        {"name": "root_node", "uuid": "l1", "type": "locator", "position": [0, 0, 0]},
    ],
    "outliner": [
        {"name": "g1", "uuid": "g1", "origin": [0, 8, 0], "rotation": [0, 45, 0],
         "children": ["b2"]},
        "b1", "b3", "b4", "l1",
    ],
    "textures": [],
}

nat_bush = {
    "meta": {"model_format": "bedrock"},
    "name": "nat_bush",
    "resolution": {"width": 16, "height": 16},
    "elements": [
        {"name": "bush", "uuid": "n1", "type": "cube",
         "from": [-8, 0, -8], "to": [8, 8, 8], "origin": [0, 0, 0],
         "rotation": [0, 0, 0], "faces": full_cube_faces(0, 0, 16, 16)},
    ],
    "outliner": ["n1"],
    "textures": [],
}

J = json.dumps

# Материал 32x32: 4 цветных квадранта (проверка ориентации UV).
def lamp_px(x, y):
    if x < 16 and y < 16:  return (255, 0, 0, 255)     # TL красный
    if x >= 16 and y < 16: return (0, 200, 0, 255)     # TR зелёный
    if x < 16:             return (0, 0, 255, 255)     # BL синий
    return (240, 240, 240, 255)                        # BR белый

strip_cols = [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 0, 255)]

entries = {
    # --- decotest (DecoCraft-стиль: loader json) ---
    "assets/decotest/blockstates/deco_lamp.json": J({"variants": {
        "facing=north": {"model": "decotest:deco_lamp", "y": 0},
        "facing=east":  {"model": "decotest:deco_lamp", "y": 90},
        "facing=south": {"model": "decotest:deco_lamp", "y": 180},
        "facing=west":  {"model": "decotest:deco_lamp", "y": 270}}}),
    "assets/decotest/models/deco_lamp.json": J({
        "loader": "deco:bbmodel", "flip-v": False, "scale": 1.0,
        "model": "decotest:models/bbmodel/lamp.bbmodel",
        "material": "decotest:block/deco_lamp",
        "textures": {"texture0": "minecraft:block/dirt",
                     "particle": "minecraft:block/dirt"}}),
    "assets/decotest/models/bbmodel/lamp.bbmodel": J(lamp),
    "assets/decotest/textures/block/deco_strip.png.mcmeta": J({"animation": {}}),

    # --- natmin (DecoCraft Nature-стиль: пустые elements + реестр) ---
    "assets/natmin/decocraft.json": J(["items.json"]),
    "assets/natmin/items.json": J({"models": [
        {"name": "Nat Bush", "decoref": "nat_bush", "model": "nat_bush",
         "material": "nat_bush", "scale": 1.0}]}),
    "assets/natmin/blockstates/nat_bush.json": J({"variants": {
        "facing=north": {"model": "natmin:nat_bush", "y": 0},
        "facing=east":  {"model": "natmin:nat_bush", "y": 90}}}),
    "assets/natmin/models/nat_bush.json": J({
        "parent": "block/block",
        "textures": {"particle": "minecraft:block/stone"},
        "elements": []}),
    "assets/natmin/models/bbmodel/nat_bush.bbmodel": J(nat_bush),

    # Метка модлоадера, чтобы сканер определял тип
    "META-INF/mods.toml": "modLoader=\"javafml\"\n",
}

pngs = {
    "assets/decotest/textures/block/deco_lamp.png": png(32, 32, lamp_px),
    "assets/decotest/textures/block/deco_strip.png":
        png(16, 64, lambda x, y: strip_cols[y // 16]),
    "assets/decotest/textures/block/deco_tall.png":
        png(16, 32, lambda x, y: (255, 0, 255, 255) if y < 16 else (0, 255, 255, 255)),
    "assets/natmin/textures/block/nat_bush.png":
        png(16, 16, lambda x, y: (255, 220, 40, 255)),
}
entries.update(pngs)

out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(__file__), "data", "mods", "decomini.jar")
os.makedirs(os.path.dirname(out), exist_ok=True)
# Упакованный (deflate) zip — как настоящий jar.
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for name, data in entries.items():
        z.writestr(name, data.encode("utf-8") if isinstance(data, str) else data)
print(f"wrote {out} ({os.path.getsize(out)} bytes, {len(entries)} entries)")
