#!/usr/bin/env python3
"""Convert a photo into src/background.h (RGB565, 320x240) for the screensaver.

    pip install pillow
    python tools/jpg2background.py assets/screensaver.jpg
    python tools/jpg2background.py photo.jpg --center 0.45

The photo is cropped to the screen's 4:3 shape around --center (0 = keep the
left edge, 1 = keep the right edge) and scaled to 320x240. A preview is
written to assets/screensaver_preview.png so you can check the crop.
"""
import argparse
from pathlib import Path

from PIL import Image

W, H = 320, 240

ap = argparse.ArgumentParser()
ap.add_argument("image")
ap.add_argument("--center", type=float, default=0.5, help="horizontal crop position 0..1")
ap.add_argument("--out", default=str(Path(__file__).resolve().parent.parent / "src" / "background.h"))
a = ap.parse_args()

img = Image.open(a.image).convert("RGB")
iw, ih = img.size
if iw / ih > W / H:  # too wide: crop the sides
    cw = round(ih * W / H)
    x0 = round((iw - cw) * min(max(a.center, 0.0), 1.0))
    img = img.crop((x0, 0, x0 + cw, ih))
else:  # too tall: crop top and bottom evenly
    ch = round(iw * H / W)
    y0 = (ih - ch) // 2
    img = img.crop((0, y0, iw, y0 + ch))
img = img.resize((W, H), Image.LANCZOS)
img.save(Path(__file__).resolve().parent.parent / "assets" / "screensaver_preview.png")

raw = img.tobytes()
words = []
for i in range(0, len(raw), 3):
    r, g, b = raw[i], raw[i + 1], raw[i + 2]
    words.append(f"0x{((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3):04X}")

with open(a.out, "w") as f:
    f.write("#pragma once\n#include <pgmspace.h>\n\n")
    f.write(f"#define BG_W {W}\n#define BG_H {H}\n")
    f.write("const uint16_t background_bitmap[] PROGMEM = {\n")
    for i in range(0, len(words), 16):
        f.write("  " + ", ".join(words[i:i + 16]) + ",\n")
    f.write("};\n")
print(f"wrote {a.out} ({W}x{H})")
