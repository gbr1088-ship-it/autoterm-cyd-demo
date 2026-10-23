"""Makes every copy of the Adventure Van Life logo from the artwork in
assets/avl_logo_original.jpg (black lettering on an orange glow).

    pip install pillow
    python tools/avl_logo.py

Writes:
    assets/avl_logo.png        splash: the orange tile with rounded corners
    assets/avl_logo_small.png  header: the lettering in orange, for the dark header bar
    assets/avl_ink.png         the lettering alone (black on transparent), for the screensaver
    web/icon.png               phone home-screen icon
    the tile inside web/app.html (between the <!--avl-mark--> comments)
Then run the commands it prints.
"""
import base64
import io
import re
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
ORANGE = (255, 145, 76)  # the artwork's background
GLOW = (252, 182, 140)   # its lighter centre


def ink_mask():
    """Alpha mask of the black lettering, cropped to it."""
    grey = Image.open(ROOT / "assets" / "avl_logo_original.jpg").convert("L")
    ink = grey.point(lambda v: max(0, min(255, (150 - v) * 255 // 110)))
    return ink.crop(ink.point(lambda v: 255 if v > 128 else 0).getbbox())


def glow(size):
    """The artwork's background: orange with a soft lighter centre."""
    bg = Image.new("RGB", (size, size), ORANGE)
    spot = Image.new("L", (size, size), 0)
    ImageDraw.Draw(spot).ellipse((size * .22, size * .22, size * .78, size * .78), fill=255)
    bg.paste(GLOW, (0, 0), spot.filter(ImageFilter.GaussianBlur(size * .16)))
    return bg


def tile(ink, size, margin=0.1, radius=0.0):
    t = glow(size)
    m = ink.copy()
    m.thumbnail((round(size * (1 - 2 * margin)),) * 2, Image.LANCZOS)
    t.paste((0, 0, 0), ((size - m.width) // 2, (size - m.height) // 2), m)
    t = t.convert("RGBA")
    if radius:
        mask = Image.new("L", (size, size), 0)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, size - 1, size - 1), round(size * radius), fill=255)
        t.putalpha(mask)
    return t


def main():
    ink = ink_mask()
    tile(ink, 600, radius=0.08).save(ROOT / "assets" / "avl_logo.png")

    small = Image.new("RGBA", ink.size, ORANGE + (0,))
    small.putalpha(ink)
    small.save(ROOT / "assets" / "avl_logo_small.png")

    black = Image.new("RGBA", ink.size, (0, 0, 0, 0))
    black.putalpha(ink)
    black.save(ROOT / "assets" / "avl_ink.png")

    tile(ink, 180, margin=0.08).convert("RGB").save(ROOT / "web" / "icon.png", optimize=True)

    buf = io.BytesIO()
    tile(ink, 120, margin=0.08, radius=0.2).save(buf, "PNG", optimize=True)
    uri = "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()
    app = ROOT / "web" / "app.html"
    page = re.sub(r"<!--avl-mark-->.*?<!--/avl-mark-->",
                  lambda m: f'<!--avl-mark--><img class="mark" src="{uri}" alt=""><!--/avl-mark-->',
                  app.read_text(encoding="utf-8"), flags=re.S)
    app.write_text(page, encoding="utf-8")
    print("wrote assets/avl_logo*.png, assets/avl_ink.png, web/icon.png and the tile in web/app.html. Next:\n"
          "  python tools/png2rgb565.py assets/avl_logo.png --max 150 150 --small-image assets/avl_logo_small.png"
          " --small-max 80 24 --name avl_logo --out src/avl_logo.h\n"
          "  python tools/cutout2background.py assets/avl_ink.png --name avl --top 255,178,132 --bottom 255,140,70"
          " --scale 0.9 --middle --no-shadow\n"
          "  python tools/web2header.py")


if __name__ == "__main__":
    main()
