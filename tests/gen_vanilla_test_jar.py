#!/usr/bin/env python3
"""Генератор tests/data/vanilla/vanilla_test.jar — минимальный «ванильный» jar для vw_test_models.

Полностью офлайн и детерминирован: геометрия блокстейтов/моделей лежит рядом в
tests/vanilla_assets.json, текстуры генерируются процедурно (16x16 RGBA,
цвет — стабильная функция от имени, crc32, а не от randomized hash()).

Никакой сети: CI не должен зависеть от стороннего сайта, а обычная сборка —
скачивать десятки файлов при каждом запуске.
"""
import json
import os
import struct
import sys
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "vanilla_assets.json")
OUT = os.path.join(HERE, "data", "vanilla", "vanilla_test.jar")

# Фиксированное время внутри zip → побайтово воспроизводимый jar.
ZIP_DATE = (2024, 1, 1, 0, 0, 0)


def png_rgba(w, h, pixel):
    """Минимальный PNG (8 бит, RGBA). pixel(x, y) -> (r, g, b, a)."""
    def chunk(tag, data):
        body = tag + data
        return (struct.pack(">I", len(data)) + body +
                struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))

    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter: None
        for x in range(w):
            raw.extend(pixel(x, y))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
            chunk(b"IEND", b""))


def stable_color(name):
    h = zlib.crc32(name.encode("utf-8")) & 0xFFFFFFFF
    # Держим каналы в средней яркости: тесты считают средние цвета текстур.
    r = 64 + ((h >> 16) & 0x7F)
    g = 64 + ((h >> 8) & 0x7F)
    b = 64 + (h & 0x7F)
    return r, g, b


def texture_png(ref):
    """Детерминированная заглушка текстуры для ассета ref (напр. block/stone)."""
    if ref.startswith("colormap/"):
        # 16x16 «колормапа»: плавный градиент, как у ванильных grass/foliage.
        base = (0x91, 0xBD, 0x59) if ref.endswith("grass") else (0x77, 0xAB, 0x2F)

        def px(x, y):
            return (max(0, base[0] - x * 2), max(0, base[1] - y * 2), base[2], 255)

        return png_rgba(16, 16, px)

    r, g, b = stable_color(ref)
    # У «стеклянных»/решётчатых текстур делаем прозрачные пиксели, чтобы
    # работали cutout-пути (листва, панели, решётка).
    cutout = any(k in ref for k in ("glass", "leaves", "iron_bars", "ladder",
                                    "fire", "grass_block_side_overlay"))

    def px(x, y):
        a = 255
        if cutout and ((x + y) % 4 == 0):
            a = 0
        # лёгкий шум, чтобы средний цвет не совпадал у соседних тайлов
        d = ((x * 7 + y * 13) % 16) - 8
        return (max(0, min(255, r + d)), max(0, min(255, g + d)),
                max(0, min(255, b + d)), a)

    return png_rgba(16, 16, px)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else OUT
    with open(ASSETS, "r", encoding="utf-8") as f:
        data = json.load(f)

    entries = {}
    for name, obj in sorted(data["blockstates"].items()):
        entries[f"assets/minecraft/blockstates/{name}.json"] = \
            json.dumps(obj, sort_keys=True, separators=(",", ":")).encode()
    for name, obj in sorted(data["models"].items()):
        entries[f"assets/minecraft/models/{name}.json"] = \
            json.dumps(obj, sort_keys=True, separators=(",", ":")).encode()
    for ref in sorted(set(data["textures"])):
        entries[f"assets/minecraft/textures/{ref}.png"] = texture_png(ref)
    entries["fabric.mod.json"] = json.dumps(
        {"id": "minecraft", "version": "1.20.1"}, sort_keys=True,
        separators=(",", ":")).encode()

    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    tmp = out + ".tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as z:
        for name in sorted(entries):
            info = zipfile.ZipInfo(name, date_time=ZIP_DATE)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, entries[name])
    os.replace(tmp, out)

    print(f"wrote {out} ({os.path.getsize(out)} bytes, {len(entries)} entries): "
          f"blockstates {len(data['blockstates'])}, models {len(data['models'])}, "
          f"textures {len(data['textures'])}")


if __name__ == "__main__":
    main()
