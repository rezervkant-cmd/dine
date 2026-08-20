#!/usr/bin/env python3
"""Генератор тестовых данных VoxelWays:
  - мир формата 1.18+ (sections/block_states/palette/data, DataVersion 3465)
  - мир legacy <=1.12.2 (Level.Sections/Blocks/Data)
  - тестовый jar-мод с blockstate/model/texture
Выход: tests/data/{world_modern,world_legacy}/..., tests/data/mods/testmod.jar
"""
import gzip, json, os, struct, zlib, io, zipfile, shutil

OUT = os.path.join(os.path.dirname(__file__), "data")

# ---------------- NBT writer ----------------
def tag_payload(v):
    """Возвращает (type_id, payload_bytes)."""
    if isinstance(v, dict):
        b = b""
        for k, x in v.items():
            t, p = tag_payload(x)
            b += struct.pack(">BH", t, len(k)) + k.encode() + p
        return 10, b + b"\x00"
    if isinstance(v, list):
        if not v:
            return 9, struct.pack(">Bi", 0, 0)
        t0, _ = tag_payload(v[0])
        b = struct.pack(">Bi", t0, len(v))
        for x in v:
            _, p = tag_payload(x)
            b += p
        return 9, b
    if isinstance(v, str):
        e = v.encode()
        return 8, struct.pack(">H", len(e)) + e
    if isinstance(v, IntTag):   return 3, struct.pack(">i", v.v)
    if isinstance(v, ByteTag):  return 1, struct.pack(">b", v.v)
    if isinstance(v, LongArr):  return 12, struct.pack(">i", len(v.v)) + b"".join(struct.pack(">q", x) for x in v.v)
    if isinstance(v, ByteArr):  return 7, struct.pack(">i", len(v.v)) + bytes((x & 0xFF) for x in v.v)
    raise TypeError(type(v))

class IntTag:
    def __init__(s, v): s.v = v
class ByteTag:
    def __init__(s, v): s.v = v
class LongArr:
    def __init__(s, v): s.v = v
class ByteArr:
    def __init__(s, v): s.v = v

def nbt_file(root_name, compound):
    t, p = tag_payload(compound)
    return struct.pack(">BH", 10, len(root_name)) + root_name.encode() + p

# ---------------- упаковка индексов (1.18 padded) ----------------
def pack_padded(indices, bits):
    per = 64 // bits
    out = []
    for i in range(0, len(indices), per):
        w = 0
        for j, idx in enumerate(indices[i:i+per]):
            w |= (idx & ((1 << bits) - 1)) << (j * bits)
        if w >= 1 << 63:
            w -= 1 << 64
        out.append(w)
    return out

# ---------------- region writer ----------------
def xxh32(data, seed=0x9747B28C):
    # XXH32 compatible with lz4-java; Minecraft stores its low 28 bits.
    p1, p2, p3, p4, p5 = 0x9E3779B1, 0x85EBCA77, 0xC2B2AE3D, 0x27D4EB2F, 0x165667B1
    mask = 0xFFFFFFFF
    def rol(x, n): return ((x << n) | (x >> (32 - n))) & mask
    def rd(p): return struct.unpack_from("<I", data, p)[0]
    def rnd(a, v): return (rol((a + v * p2) & mask, 13) * p1) & mask
    n, p = len(data), 0
    if n >= 16:
        v1, v2, v3, v4 = (seed + p1 + p2) & mask, (seed + p2) & mask, seed, (seed - p1) & mask
        while p <= n - 16:
            v1, v2, v3, v4 = rnd(v1, rd(p)), rnd(v2, rd(p+4)), rnd(v3, rd(p+8)), rnd(v4, rd(p+12))
            p += 16
        h = (rol(v1,1) + rol(v2,7) + rol(v3,12) + rol(v4,18)) & mask
    else: h = (seed + p5) & mask
    h = (h + n) & mask
    while p + 4 <= n:
        h = (rol((h + rd(p) * p3) & mask, 17) * p4) & mask; p += 4
    while p < n:
        h = (rol((h + data[p] * p5) & mask, 11) * p1) & mask; p += 1
    h ^= h >> 15; h = (h * p2) & mask; h ^= h >> 13; h = (h * p3) & mask; h ^= h >> 16
    return h & mask

def lz4_raw_block(data):
    head = b"LZ4Block" + bytes([0x10])
    head += struct.pack("<II", len(data), len(data))
    head += struct.pack("<I", xxh32(data) & 0x0FFFFFFF)
    return head + data + b"LZ4Block" + bytes(13)

def write_region(path, chunks, compression=2):
    """chunks: {(lx,lz): nbt_bytes}; compression 2=zlib, 4=LZ4Block raw."""
    sectors = [b""] * 2  # заголовок
    loc = bytearray(4096)
    ts = bytes(4096)
    body = b""
    sector_off = 2
    for (lx, lz), nbt in chunks.items():
        comp = zlib.compress(nbt) if compression == 2 else lz4_raw_block(nbt)
        rec = struct.pack(">IB", len(comp) + 1, compression) + comp
        pad = (-len(rec)) % 4096
        rec += bytes(pad)
        nsec = len(rec) // 4096
        i = (lz * 32 + lx) * 4
        loc[i:i+4] = struct.pack(">I", (sector_off << 8) | nsec)
        body += rec
        sector_off += nsec
    with open(path, "wb") as f:
        f.write(bytes(loc) + ts + body)

# ---------------- современный мир (1.18+) ----------------
def make_modern():
    d = os.path.join(OUT, "world_modern")
    os.makedirs(os.path.join(d, "region"), exist_ok=True)

    level = {"Data": {
        "LevelName": "TestModern", "DataVersion": IntTag(3465),
        "Version": {"Name": "1.20.1", "Id": IntTag(3465)},
        "SpawnX": IntTag(8), "SpawnY": IntTag(4), "SpawnZ": IntTag(8),
    }}
    with gzip.open(os.path.join(d, "level.dat"), "wb") as f:
        f.write(nbt_file("", level))

    palette = [
        {"Name": "minecraft:air"},
        {"Name": "minecraft:stone"},
        {"Name": "minecraft:grass_block", "Properties": {"snowy": "false"}},
        {"Name": "minecraft:water", "Properties": {"level": "0"}},
        {"Name": "testmod:ruby_block"},
    ]
    # Секция y=0: слои y0-1 камень, y2 трава, в углу вода и рубиновый блок
    idx = [0] * 4096
    for y in range(3):
        for z in range(16):
            for x in range(16):
                i = (y << 8) | (z << 4) | x
                idx[i] = 1 if y < 2 else 2
    idx[(3 << 8) | (0 << 4) | 0] = 3   # вода на y=3
    idx[(3 << 8) | (1 << 4) | 1] = 4   # ruby на y=3
    bits = 4
    data = pack_padded(idx, bits)

    chunk = {
        "DataVersion": IntTag(3465),
        "xPos": IntTag(0), "zPos": IntTag(0), "yPos": IntTag(-4),
        "Status": "minecraft:full",
        "sections": [
            {"Y": ByteTag(0),
             "block_states": {"palette": palette, "data": LongArr(data)}},
            {"Y": ByteTag(1),
             "block_states": {"palette": [{"Name": "minecraft:air"}]}},
        ],
    }
    chunk_bytes = nbt_file("", chunk)
    write_region(os.path.join(d, "region", "r.0.0.mca"), {(0, 0): chunk_bytes})

    # Same data through Minecraft's type-4 LZ4Block container. This fixture
    # protects the decoder from regressing to support only standard LZ4 frames.
    lz4d = os.path.join(OUT, "world_lz4")
    os.makedirs(os.path.join(lz4d, "region"), exist_ok=True)
    with gzip.open(os.path.join(lz4d, "level.dat"), "wb") as f:
        f.write(nbt_file("", {"Data": {"LevelName": "TestLz4", "DataVersion": IntTag(3465),
                                       "Version": {"Name": "1.20.5", "Id": IntTag(3837)},
                                       "SpawnX": IntTag(8), "SpawnY": IntTag(4), "SpawnZ": IntTag(8)}}))
    write_region(os.path.join(lz4d, "region", "r.0.0.mca"), {(0, 0): chunk_bytes}, 4)

    # Dimension discovery fixture: vanilla Nether plus a datapack dimension.
    for rel in ("DIM-1/region", "dimensions/testmod/moon/region"):
        target = os.path.join(d, rel)
        os.makedirs(target, exist_ok=True)
        shutil.copyfile(os.path.join(d, "region", "r.0.0.mca"), os.path.join(target, "r.0.0.mca"))
    print("modern/LZ4 worlds ->", d, lz4d)

# ---------------- legacy мир (<=1.12.2) ----------------
def make_legacy():
    d = os.path.join(OUT, "world_legacy")
    os.makedirs(os.path.join(d, "region"), exist_ok=True)
    with gzip.open(os.path.join(d, "level.dat"), "wb") as f:
        f.write(nbt_file("", {"Data": {"LevelName": "TestLegacy",
                                       "SpawnX": IntTag(8), "SpawnY": IntTag(4), "SpawnZ": IntTag(8)}}))

    blocks = [0] * 4096
    data_nib = [0] * 4096
    for y in range(3):
        for z in range(16):
            for x in range(16):
                i = (y << 8) | (z << 4) | x
                blocks[i] = 1 if y < 2 else 2       # stone/grass
    blocks[(3 << 8) | 5] = 35                        # шерсть
    data_nib[(3 << 8) | 5] = 14                      # красная
    packed = [ (data_nib[i] & 0xF) | ((data_nib[i+1] & 0xF) << 4) for i in range(0, 4096, 2) ]

    chunk = {"Level": {
        "xPos": IntTag(0), "zPos": IntTag(0),
        "Sections": [{
            "Y": ByteTag(0),
            "Blocks": ByteArr(blocks),
            "Data": ByteArr(packed),
        }],
    }}
    write_region(os.path.join(d, "region", "r.0.0.mca"),
                 {(0, 0): nbt_file("", chunk)})
    print("legacy world ->", d)

# ---------------- тестовый мод ----------------
def make_mod():
    d = os.path.join(OUT, "mods")
    os.makedirs(d, exist_ok=True)
    # 16x16 PNG (сплошной рубиновый цвет) — пишем вручную
    import binascii
    def png_chunk(t, payload):
        return struct.pack(">I", len(payload)) + t + payload + \
               struct.pack(">I", binascii.crc32(t + payload) & 0xFFFFFFFF)
    w = h = 16
    raw = b""
    for _ in range(h):
        raw += b"\x00" + (b"\xE0\x11\x5F\xFF" * w)   # RGBA
    png = (b"\x89PNG\r\n\x1a\n" +
           png_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
           png_chunk(b"IDAT", zlib.compress(raw)) +
           png_chunk(b"IEND", b""))

    jar = os.path.join(d, "testmod.jar")
    with zipfile.ZipFile(jar, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("fabric.mod.json", json.dumps({"id": "testmod", "version": "1.0"}))
        z.writestr("assets/testmod/blockstates/ruby_block.json",
                   json.dumps({"variants": {"": {"model": "testmod:block/ruby_block"}}}))
        z.writestr("assets/testmod/models/block/ruby_block.json",
                   json.dumps({"parent": "minecraft:block/cube_all",
                               "textures": {"all": "testmod:block/ruby_block"}}))
        z.writestr("assets/testmod/textures/block/ruby_block.png", png)
    print("mod ->", jar)

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    make_modern()
    make_legacy()
    make_mod()
    # Дополнительные фикстуры (decomini.jar, vanilla_test.jar) генерируются
    # отдельными скриптами — их запускает CMake-цель vw_test_data. Здесь их
    # не вызываем, чтобы одна и та же работа не делалась по нескольку раз:
    #   python3 tests/gen_deco_test_jar.py tests/data/mods/decomini.jar
    #   python3 tests/gen_vanilla_test_jar.py
