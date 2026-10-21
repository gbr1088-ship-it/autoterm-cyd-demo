#!/usr/bin/env python3
"""Convert a PNG/JPG logo into src/logo.h (RGB565) for the splash screen.

    pip install pillow
    python tools/png2rgb565.py autoterm_logo.png            # fits into 300x150
    python tools/png2rgb565.py autoterm_logo.png --max 280 120 --bg 0x0841

Transparent pixels are filled with --bg (default: the UI background colour).
Once src/logo.h exists the firmware uses it automatically.
"""
import argparse
from pathlib import Path

from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument("image")
ap.add_argument("--max", nargs=2, type=int, default=[300, 150], metavar=("W", "H"))
ap.add_argument("--bg", default="0x0841", help="RGB565 background for transparency")
ap.add_argument("--out", default=str(Path(__file__).resolve().parent.parent / "src" / "logo.h"))
a = ap.parse_args()

bg565 = int(a.bg, 16)
bg = ((bg565 >> 11) << 3, ((bg565 >> 5) & 0x3F) << 2, (bg565 & 0x1F) << 3)

img = Image.open(a.image).convert("RGBA")
img.thumbnail(tuple(a.max), Image.LANCZOS)
canvas = Image.new("RGBA", img.size, bg + (255,))
canvas.alpha_composite(img)
img = canvas.convert("RGB")
w, h = img.size

raw = img.tobytes()
words = []
for i in range(0, len(raw), 3):
    r, g, b = raw[i], raw[i + 1], raw[i + 2]
    words.append(f"0x{((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3):04X}")

with open(a.out, "w") as f:
    f.write("#pragma once\n#include <pgmspace.h>\n\n")
    f.write(f"#define LOGO_W {w}\n#define LOGO_H {h}\n\n")
    f.write("const uint16_t logo_bitmap[] PROGMEM = {\n")
    for i in range(0, len(words), 16):
        f.write("  " + ", ".join(words[i:i + 16]) + ",\n")
    f.write("};\n")
print(f"wrote {a.out} ({w}x{h})")
