#!/usr/bin/env python3
"""Cut the Lomax badge out of a photo of the sticker, for the start-up screen.

    pip install pillow
    python tools/lomax_logo.py

Reads assets/lomax_logo_photo.jpg (the winged LoMAX / ENGLAND / STOURBRIDGE
sticker on a blue cloth). The cloth is told apart by its blue tint; the
badge is what's left, with any holes filled. It is made black and white with
the contrast stretched, so the sticker's lighting doesn't show, and written
with a transparent background to assets/lomax_logo.png. Then the headers:

    python tools/png2rgb565.py assets/lomax_logo.png --max 300 200 --bg 0x0841 --name lomax_logo --out src/lomax_logo.h
    python tools/png2rgb565.py assets/lomax_logo.png --max 220 160 --bg 0x0841 --name lomax_logo --out src/lomax_logo_round.h
"""
from collections import deque
from pathlib import Path

from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "lomax_logo_photo.jpg"
OUT = ROOT / "assets" / "lomax_logo.png"
CLOTH_TINT = 15  # blue minus red: the cloth is 25 and up, the sticker under 10


def flood(w, h, seeds, ok):
    """Pixels reachable from the seeds through pixels where ok(x, y)."""
    seen = [[False] * w for _ in range(h)]
    q = deque(seeds)
    while q:
        x, y = q.popleft()
        if not (0 <= x < w and 0 <= y < h) or seen[y][x] or not ok(x, y):
            continue
        seen[y][x] = True
        q.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
    return seen


def main():
    img = Image.open(SRC).convert("RGB")
    w, h = img.size
    grey = img.convert("L")
    g = grey.load()
    px = img.load()
    edge = [(x, y) for x in range(w) for y in (0, h - 1)] + [(x, y) for y in range(h) for x in (0, w - 1)]
    # the cloth: blue-tinted pixels joined to the photo's edge. The sticker is
    # grey, white or black (no tint), even where its feathers are shaded.
    cloth = flood(w, h, edge, lambda x, y: px[x, y][2] - px[x, y][0] > CLOTH_TINT)
    # the sticker: the rest that joins its lightest middle pixel (no stray
    # flecks), and whatever that encloses
    seed = max(((x, y) for x in range(w // 4, 3 * w // 4) for y in range(h // 4, 3 * h // 4)), key=lambda p: g[p])
    body = flood(w, h, [seed], lambda x, y: not cloth[y][x])
    bg = flood(w, h, edge, lambda x, y: not body[y][x])
    mask = Image.new("L", (w, h))
    mask.putdata([0 if bg[y][x] else 255 for y in range(h) for x in range(w)])
    # drop the blue fringe round the edge, then soften it again
    mask = mask.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.GaussianBlur(0.7))
    box = mask.point(lambda v: 255 if v > 128 else 0).getbbox()

    # black and white, stretched between the sticker's darkest and lightest
    vals = sorted(v for v, m in zip(grey.tobytes(), mask.tobytes()) if m > 200)
    lo, hi = vals[len(vals) // 50], vals[-len(vals) // 50]
    grey = grey.point(lambda v: max(0, min(255, round((v - lo) * 255 / (hi - lo)))))

    out = Image.merge("RGBA", (grey, grey, grey, mask)).crop(box)
    out.save(OUT)
    print(f"wrote {OUT} ({out.size[0]}x{out.size[1]})")


if __name__ == "__main__":
    main()
