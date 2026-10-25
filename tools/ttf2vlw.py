#!/usr/bin/env python3
"""Make the 7" screen's anti-aliased fonts: TFT_eSPI smooth fonts (.vlw, as
arrays in flash for tft.loadFont()) at the pixel sizes the phone app uses.

    pip install pillow
    python tools/ttf2vlw.py

The font is Noto Sans Bold (SIL Open Font License), the ASCII subset that
comes with TFT_eSPI (examples/Smooth Graphics/Arc_meter_demo/NotoSans_Bold.h),
kept as assets/NotoSans-Bold-ascii.ttf. Writes src/fonts7.h.
"""
import re
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
TTF = ROOT / "assets" / "NotoSans-Bold-ascii.ttf"
ASCII = "".join(chr(c) for c in range(32, 127))
DIGITS = " 0123456789.-:/%C"
# name: (pixel size, characters)
FONTS = {
    "F7_11": (11, ASCII),   # small uppercase labels
    "F7_13": (13, ASCII),   # status line, modes, tabs, notes
    "F7_15": (15, ASCII),   # body text
    "F7_17": (17, ASCII),   # title, power button
    "F7_20": (20, ASCII),   # stat values
    "F7_30": (30, DIGITS),  # setpoint
    "F7_44": (44, DIGITS),  # chain counter
    "F7_64": (64, DIGITS),  # cabin temperature, clock
}


def extract_ttf():
    """The TrueType file inside TFT_eSPI's example header, if not done yet."""
    if TTF.exists():
        return
    src = next((ROOT / ".pio" / "libdeps").glob("*/TFT_eSPI/examples/Smooth Graphics/Arc_meter_demo/NotoSans_Bold.h"))
    body = src.read_text().split("{", 1)[1]
    TTF.write_bytes(bytes(int(h, 16) for h in re.findall(r"0x([0-9a-fA-F]{2})", body)))


def em_scale():
    """The subset file's em is about twice the real one, so a size asked of
    PIL comes out half as big. Measure it against Noto Sans's cap height
    (0.714 em) so F7_<n> really is the app's n px."""
    cap = -ImageFont.truetype(str(TTF), 100).getbbox("H", anchor="ls")[1]
    return 71.4 / cap


def vlw(size, chars):
    font = ImageFont.truetype(str(TTF), round(size * em_scale()))
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
    extract_ttf()
    out = ROOT / "src" / "fonts7.h"
    with open(out, "w") as f:
        f.write("// Noto Sans Bold (SIL Open Font License) as TFT_eSPI smooth fonts for the 7\" screen,\n"
                "// made by tools/ttf2vlw.py. F7_<n> is n pixels; F7_30 and up have digits only.\n"
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
