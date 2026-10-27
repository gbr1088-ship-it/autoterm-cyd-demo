#!/usr/bin/env python3
"""Convert a PNG/JPG logo into a header of RGB565 bitmaps for the firmware.

    pip install pillow
    python tools/png2rgb565.py logo.png --max 280 120 --small-max 100 20 --name logo --out src/logo.h
    python tools/png2rgb565.py logo_with_tagline.png --small-image logo_plain.png
    python tools/png2rgb565.py assets/avl_logo.png --small-image assets/avl_logo_small.png \
        --max 150 150 --small-max 80 24 --name avl_logo --out src/avl_logo.h

Two copies are written: a large one for the splash screen (bg --bg) and a
small one for the header bar (bg --small-bg). Transparent pixels are filled
with those colours.
"""
import argparse
from pathlib import Path

from PIL import Image


def rgb565_to_rgb(c):
    return ((c >> 11) << 3, ((c >> 5) & 0x3F) << 2, (c & 0x1F) << 3)


def convert(src, max_size, bg565, grow=False):
    img = src.copy()
    if grow:  # enlarge a small picture to fill max_size (thumbnail only shrinks)
        k = min(max_size[0] / img.width, max_size[1] / img.height)
        if k > 1:
            img = img.resize((round(img.width * k), round(img.height * k)), Image.LANCZOS)
    img.thumbnail(tuple(max_size), Image.LANCZOS)
    canvas = Image.new("RGBA", img.size, rgb565_to_rgb(bg565) + (255,))
    canvas.alpha_composite(img)
    raw = canvas.convert("RGB").tobytes()
    words = []
    for i in range(0, len(raw), 3):
        r, g, b = raw[i], raw[i + 1], raw[i + 2]
        words.append(f"0x{((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3):04X}")
    return img.size, words


def emit(f, name, prefix, size, words):
    w, h = size
    f.write(f"#define {prefix}_W {w}\n#define {prefix}_H {h}\n")
    f.write(f"const uint16_t {name}[] PROGMEM = {{\n")
    for i in range(0, len(words), 16):
        f.write("  " + ", ".join(words[i:i + 16]) + ",\n")
    f.write("};\n\n")


ap = argparse.ArgumentParser()
ap.add_argument("image")
ap.add_argument("--small-image", help="different artwork for the header (e.g. without tagline)")
ap.add_argument("--max", nargs=2, type=int, default=[280, 130], metavar=("W", "H"))
ap.add_argument("--small-max", nargs=2, type=int, default=[110, 22], metavar=("W", "H"))
ap.add_argument("--bg", default="0x1061", help="RGB565 splash background (C_BG)")
ap.add_argument("--small-bg", default="0x18C2", help="RGB565 header background (C_PANEL)")
ap.add_argument("--grow", action="store_true", help="enlarge the splash copy if it's smaller than --max")
ap.add_argument("--name", default="logo", help="names in the header: <name>_bitmap, <NAME>_W, ...")
ap.add_argument("--out", default=str(Path(__file__).resolve().parent.parent / "src" / "logo.h"))
a = ap.parse_args()

src = Image.open(a.image).convert("RGBA")
big = convert(src, a.max, int(a.bg, 16), a.grow)
small_src = Image.open(a.small_image).convert("RGBA") if a.small_image else src
small = convert(small_src, a.small_max, int(a.small_bg, 16))

with open(a.out, "w") as f:
    f.write("#pragma once\n#include <pgmspace.h>\n\n")
    emit(f, f"{a.name}_bitmap", a.name.upper(), *big)
    emit(f, f"{a.name}_small_bitmap", f"{a.name.upper()}_SMALL", *small)
print(f"wrote {a.out} (splash {big[0][0]}x{big[0][1]}, header {small[0][0]}x{small[0][1]})")
