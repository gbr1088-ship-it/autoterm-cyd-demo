#!/usr/bin/env python3
"""Make the Lomax dashboard's anti-aliased fonts: TFT_eSPI smooth fonts (.vlw,
as arrays in flash for loadFont()).

    pip install pillow
    python tools/lomax_fonts.py

The font is Liberation Serif Bold (SIL Open Font License), kept as
assets/LiberationSerif-Bold.ttf. LX_<n> is n pixels to the em, so its digits
are about two thirds of n tall. Writes src/lomax_fonts.h.
"""
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
TTF = ROOT / "assets" / "LiberationSerif-Bold.ttf"
ASCII = "".join(chr(c) for c in range(32, 127))
CAPS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
# name: (pixel size, characters)
FONTS = {
    "LX_11": (11, ASCII),  # small labels, km/h scale
    "LX_15": (15, ASCII),  # numerals on the side-by-side dials, odometer
    "LX_21": (21, ASCII),  # numerals on the big dials
    "LX_48": (48, CAPS),   # the LOMAX name on the start-up screen
}


def vlw(size, chars):
    font = ImageFont.truetype(str(TTF), size)
    ascent, descent = font.getmetrics()
    head, bitmaps = [], b""
    for ch in chars:
        x0, y0, x1, y1 = font.getbbox(ch, anchor="ls") if ch != " " else (0, 0, 0, 0)
        w, h = x1 - x0, y1 - y0
        if w > 0 and h > 0:
            img = Image.new("L", (w, h), 0)
            ImageDraw.Draw(img).text((-x0, -y0), ch, font=font, fill=255, anchor="ls")
            bitmaps += img.tobytes()
        else:
            w = h = 0
        # unicode, height, width, x advance, top above the baseline, left offset, unused
        head.append(struct.pack(">7i", ord(ch), h, w, round(font.getlength(ch)), -y0, x0, 0))
    return struct.pack(">6i", len(chars), 11, size, 0, ascent, descent) + b"".join(head) + bitmaps


def main():
    out = ROOT / "src" / "lomax_fonts.h"
    with open(out, "w") as f:
        f.write("// Liberation Serif Bold (SIL Open Font License) as TFT_eSPI smooth fonts for the\n"
                "// Lomax dashboard, made by tools/lomax_fonts.py. LX_<n> is n pixels to the em.\n"
                "#pragma once\n#include <pgmspace.h>\n\n")
        for name, (size, chars) in FONTS.items():
            data = vlw(size, chars)
            f.write(f"const uint8_t {name}[] PROGMEM = {{  // {len(data)} bytes\n")
            for i in range(0, len(data), 24):
                f.write("  " + ", ".join(f"0x{b:02X}" for b in data[i:i + 24]) + ",\n")
            f.write("};\n\n")
            print(f"{name}: {size} px, {len(chars)} characters, {len(data)} bytes")
    print("wrote", out)


if __name__ == "__main__":
    main()
