#!/bin/sh
# Preview the Lomax dashboard's screens as PNGs, without the board.
#   sh tools/lomax_preview/build.sh        (needs g++ and python3 with pillow)
# TFT_eSPI comes from the PlatformIO download (pio run -e lomax fetches it),
# or set TFT_ESPI to a copy of it. The pictures land in tools/lomax_preview/out.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
TFT=${TFT_ESPI:-$ROOT/.pio/libdeps/lomax/TFT_eSPI}
OUT=$HERE/out
mkdir -p "$OUT"
DEFS="-DUSER_SETUP_LOADED=1 -DILI9341_DRIVER=1 -DTFT_WIDTH=240 -DTFT_HEIGHT=320 -DTFT_DC=2 -DTFT_CS=15
      -DLOAD_GLCD=1 -DLOAD_FONT2=1 -DSMOOTH_FONT=1 -DDISABLE_ALL_LIBRARY_WARNINGS=1"
# the CYD's screens, then the round board's (round_*.png)
for BOARD in "" "-DBOARD_ROUND=1"; do
  g++ -O2 -std=gnu++17 -w $DEFS $BOARD -I"$HERE/stubs" -I"$TFT" -I"$ROOT/src" \
      "$HERE/preview.cpp" "$TFT/TFT_eSPI.cpp" -o "$OUT/preview"
  (cd "$OUT" && ./preview)
done
python3 - "$OUT" <<'PY'
import sys, glob, os
from PIL import Image
for f in glob.glob(os.path.join(sys.argv[1], "*.rgb565")):
    base, size = f[:-7].rsplit("_", 1)
    w, h = map(int, size.split("x"))
    raw = open(f, "rb").read()
    img = Image.new("RGB", (w, h))
    px = img.load()
    for i in range(w * h):
        v = raw[2 * i] | raw[2 * i + 1] << 8
        px[i % w, i // w] = ((v >> 11) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3)
    if os.path.basename(base).startswith("round_"):  # the glass is round: black outside it
        for y in range(h):
            for x in range(w):
                if (x - w / 2 + 0.5) ** 2 + (y - h / 2 + 0.5) ** 2 > (w / 2) ** 2:
                    px[x, y] = (0, 0, 0)
    png = base + ".png"
    img.resize((w * 2, h * 2), Image.NEAREST).save(png)
    os.remove(f)
    print("wrote", png)
PY
