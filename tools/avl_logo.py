"""Draws the Adventure Van Life logo and writes every copy the project uses.

    pip install pillow fonttools resvg-py
    python tools/avl_logo.py

The badge is a camper van in front of mountains and a low sun, inside a pine
green circle; the wordmark is Barlow Condensed (assets/fonts, SIL Open Font
License), turned into outlines so the SVGs need no fonts. Writes:
    assets/avl_mark.svg, assets/avl_logo.svg   badge, and badge + wordmark
    assets/avl_logo.png, assets/avl_logo_small.png, assets/avl_saver.png
    web/icon.png                               phone home-screen icon
    the badge inside web/app.html (between the <!--avl-mark--> comments)
Then run png2rgb565.py / cutout2background.py / web2header.py as printed.
"""
import io
import re
from pathlib import Path

import resvg_py
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
FONTS = ROOT / "assets" / "fonts"

PINE = "#123b37"      # badge ground
RIDGE = "#2d6e62"     # far mountains
HILL = "#4c9b85"      # near hills
SUN = "#f4a340"       # low sun, and VAN LIFE
CREAM = "#f3ede2"     # van, snow, and ADVENTURE
TYRE = "#0a0d12"      # the screen's background colour

# 100 x 100 badge: sun and mountains clipped to the circle, the van standing
# over its lower edge
MARK = f"""
<defs><clipPath id="avl-c"><circle cx="50" cy="50" r="46"/></clipPath></defs>
<circle cx="50" cy="50" r="48" fill="{PINE}"/>
<g clip-path="url(#avl-c)">
  <circle cx="66" cy="38" r="15" fill="{SUN}"/>
  <path d="M-2 76 L24 42 L38 57 L55 30 L79 61 L102 46 V102 H-2 Z" fill="{RIDGE}"/>
  <path d="M55 30 L48.5 39.5 L53 37.5 L56 41.5 L61 37.5 Z" fill="{CREAM}"/>
  <path d="M-2 82 Q28 66 58 77 T102 74 V102 H-2 Z" fill="{HILL}"/>
</g>
<path d="M28 56 H67 Q71 56 73 59 L80 69 Q82 71.5 82 75 V81 Q82 84 79 84 H27 Q24 84 24 81 V60 Q24 56 28 56 Z" fill="{CREAM}"/>
<path d="M69.5 60 L76.5 70 H69.5 Z" fill="{PINE}"/>
<rect x="29" y="61" width="15" height="8" rx="1.6" fill="{PINE}"/>
<rect x="48" y="61" width="16" height="8" rx="1.6" fill="{PINE}"/>
<rect x="24" y="74" width="58" height="3.2" fill="{SUN}"/>
<circle cx="35" cy="85" r="6.2" fill="{TYRE}" stroke="{CREAM}" stroke-width="2.2"/>
<circle cx="70" cy="85" r="6.2" fill="{TYRE}" stroke="{CREAM}" stroke-width="2.2"/>
"""


def text_path(text, font_file, size, x, y, tracking=0.0, fill=CREAM):
    """Text as an SVG path: baseline at y, left edge at x; returns (svg, width)."""
    font = TTFont(FONTS / font_file)
    glyphs, cmap = font.getGlyphSet(), font.getBestCmap()
    scale = size / font["head"].unitsPerEm
    parts, pen_x = [], 0.0
    for ch in text:
        name = cmap[ord(ch)]
        pen = SVGPathPen(glyphs)
        glyphs[name].draw(TransformPen(pen, (scale, 0, 0, -scale, x + pen_x, y)))
        parts.append(pen.getCommands())
        pen_x += glyphs[name].width * scale + tracking
    return f'<path fill="{fill}" d="{" ".join(parts)}"/>', pen_x - tracking


def wordmark(x, top, size, sub=0.62, gap=0.24, sub_font="BarlowCondensed-SemiBold.ttf"):
    """ADVENTURE over a tracked VAN LIFE of the same width; returns (svg, width, height).
    sub and gap are VAN LIFE's size and the space between the lines, relative to size."""
    cap = size * 0.7  # Barlow's cap height
    word, w = text_path("ADVENTURE", "BarlowCondensed-ExtraBold.ttf", size, x, top + cap)
    small = size * sub
    _, plain = text_path("VAN LIFE", sub_font, small, 0, 0)
    tracking = (w - plain) / (len("VAN LIFE") - 1)
    sub_svg, _ = text_path("VAN LIFE", sub_font, small, x, top + cap + size * gap + small * 0.7, tracking, SUN)
    return word + sub_svg, w, cap + size * gap + small * 0.7


def svg(width, height, body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width:.1f} {height:.1f}" '
            f'width="{width:.1f}" height="{height:.1f}">{body}</svg>')


def render(svg_text, width):
    png = resvg_py.svg_to_bytes(svg_string=svg_text, width=width)
    return Image.open(io.BytesIO(bytes(png))).convert("RGBA")


def lockup(size, space, **lines):
    """Badge on the left, wordmark beside it, centred on the badge."""
    words, w, h = wordmark(0, 0, size, **lines)
    words, w, h = wordmark(100 + space, 50 - h / 2, size, **lines)
    return svg(100 + space + w, 100, MARK + words)


def main():
    (ROOT / "assets" / "avl_mark.svg").write_text(svg(100, 100, MARK))
    big = lockup(46, 14)
    (ROOT / "assets" / "avl_logo.svg").write_text(big)
    render(big, 1120).save(ROOT / "assets" / "avl_logo.png")
    # the header copy is only 22 px tall: as tall as the badge, with a bold
    # second line nearly the size of the first, so both survive at 22 px
    small = lockup(62, 8, sub=0.78, gap=0.16, sub_font="BarlowCondensed-ExtraBold.ttf")
    render(small, 640).save(ROOT / "assets" / "avl_logo_small.png")

    # screensaver: badge above the wordmark
    words, w, h = wordmark(0, 0, 60)
    stack_w = max(w, 140)
    words, w, h = wordmark((stack_w - w) / 2, 152, 60)
    mark = f'<g transform="translate({(stack_w - 140) / 2:.1f} 0) scale(1.4)">{MARK}</g>'
    render(svg(stack_w, 152 + h, mark + words), 900).save(ROOT / "assets" / "avl_saver.png")

    # phone icon: the badge on the app's background
    icon = render(svg(100, 100, f'<rect width="100" height="100" fill="{TYRE}"/>'
                                f'<g transform="translate(9 7) scale(.82)">{MARK}</g>'), 180)
    icon.convert("RGB").save(ROOT / "web" / "icon.png", optimize=True)

    app = ROOT / "web" / "app.html"
    inline = MARK.replace('id="avl-c"', 'id="avl-c1"').replace("url(#avl-c)", "url(#avl-c1)")
    page = re.sub(r"<!--avl-mark-->.*?<!--/avl-mark-->",
                  lambda m: '<!--avl-mark--><svg class="mark" viewBox="0 0 100 100" aria-hidden="true">'
                            + " ".join(inline.split()) + "</svg><!--/avl-mark-->",
                  app.read_text(encoding="utf-8"), flags=re.S)
    app.write_text(page, encoding="utf-8")
    print("wrote assets/avl_*.svg/png, web/icon.png and the badge in web/app.html. Next:\n"
          "  python tools/png2rgb565.py assets/avl_logo.png --small-image assets/avl_logo_small.png"
          " --small-max 80 22 --name avl_logo --out src/avl_logo.h\n"
          "  python tools/cutout2background.py assets/avl_saver.png --name avl --top 30,38,48 --bottom 10,13,18"
          " --scale 0.95 --middle --no-shadow\n"
          "  python tools/web2header.py")


if __name__ == "__main__":
    main()
