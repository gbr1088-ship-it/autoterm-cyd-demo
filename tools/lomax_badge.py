#!/usr/bin/env python3
"""The Lomax badge for the dial faces: grey and see-through levels, so the
firmware can lay it over the cream face at whatever size the dial needs.

    pip install pillow
    python tools/lomax_badge.py

Reads assets/lomax_logo.png (see tools/lomax_logo.py) and writes
src/lomax_badge.h: two bytes per pixel, grey then alpha.
"""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "lomax_logo.png"
OUT = ROOT / "src" / "lomax_badge.h"

img = Image.open(SRC).convert("RGBA")
grey = img.convert("L").tobytes()
alpha = img.getchannel("A").tobytes()
data = [f"{g},{a}" for g, a in zip(grey, alpha)]
lines = [", ".join(data[i:i + 16]) for i in range(0, len(data), 16)]
OUT.write_text(
    "// The Lomax badge for the dial faces, made by tools/lomax_badge.py\n"
    "#pragma once\n#include <pgmspace.h>\n\n"
    f"#define LOMAX_BADGE_W {img.width}\n#define LOMAX_BADGE_H {img.height}\n"
    "// grey, alpha per pixel\n"
    "const uint8_t lomax_badge[] PROGMEM = {\n  " + ",\n  ".join(lines) + "\n};\n")
print("wrote", OUT, img.size)
