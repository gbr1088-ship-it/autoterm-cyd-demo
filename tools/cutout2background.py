#!/usr/bin/env python3
"""Turn a cut-out (transparent PNG) into src/van.h (RGB565, 320x240) for the
screensaver: the subject on a light gradient with a soft shadow underneath,
sized to sit below the clock band at the top of the screen.

    pip install pillow
    python tools/cutout2background.py assets/van_cutout.png

To make a cut-out from a photo:
    pip install "rembg[cpu]"
    python -c "from rembg import remove, new_session; from PIL import Image; \
remove(Image.open('photo.jpg'), session=new_session('isnet-general-use'), \
alpha_matting=True).save('assets/van_cutout.png')"
"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

W, H = 320, 240
TOP_BAND = 56  # keep clear for the clock

ap = argparse.ArgumentParser()
ap.add_argument("image")
ap.add_argument("--top", default="236,240,245", help="gradient colour at the top (r,g,b)")
ap.add_argument("--bottom", default="196,204,215", help="gradient colour at the bottom (r,g,b)")
ap.add_argument("--name", default="van")
a = ap.parse_args()
root = Path(__file__).resolve().parent.parent
top = tuple(int(v) for v in a.top.split(","))
bottom = tuple(int(v) for v in a.bottom.split(","))

cut = Image.open(a.image).convert("RGBA")
cut = cut.crop(cut.getchannel("A").point(lambda v: 255 if v > 24 else 0).getbbox())

# fit the subject into the area under the clock band, with a margin
box_w, box_h = W - 24, H - TOP_BAND - 10
scale = min(box_w / cut.width, box_h / cut.height)
cut = cut.resize((round(cut.width * scale), round(cut.height * scale)), Image.LANCZOS)
x = (W - cut.width) // 2
y = H - 8 - cut.height

canvas = Image.new("RGBA", (W, H))
draw = ImageDraw.Draw(canvas)
for row in range(H):
    f = row / (H - 1)
    draw.line([(0, row), (W, row)], fill=tuple(round(t + (b - t) * f) for t, b in zip(top, bottom)) + (255,))

shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
ImageDraw.Draw(shadow).ellipse(
    (x + cut.width * 0.04, y + cut.height - 14, x + cut.width * 0.96, y + cut.height + 4), fill=(20, 26, 34, 120))
canvas.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(6)))
canvas.alpha_composite(cut, (x, y))
img = canvas.convert("RGB")
img.save(root / "assets" / f"{a.name}_preview.png")

raw = img.tobytes()
words = []
for i in range(0, len(raw), 3):
    r, g, b = raw[i], raw[i + 1], raw[i + 2]
    words.append(f"0x{((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3):04X}")

out = root / "src" / f"{a.name}.h"
with open(out, "w") as f:
    f.write("#pragma once\n#include <pgmspace.h>\n\n")
    f.write(f"const uint16_t {a.name}_bitmap[] PROGMEM = {{  // {W}x{H}\n")
    for i in range(0, len(words), 16):
        f.write("  " + ", ".join(words[i:i + 16]) + ",\n")
    f.write("};\n")
print(f"wrote {out} ({W}x{H})")
