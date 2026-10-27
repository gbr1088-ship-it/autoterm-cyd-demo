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
g++ -O2 -std=gnu++17 -w $DEFS -I"$HERE/stubs" -I"$TFT" -I"$ROOT/src" \
    "$HERE/preview.cpp" "$TFT/TFT_eSPI.cpp" -o "$OUT/preview"
cd "$OUT" && ./preview
python3 - "$OUT" <<'PY'
import sys, glob, os
from PIL import Image
for f in glob.glob(os.path.join(sys.argv[1], "*.rgb565")):
    raw = open(f, "rb").read()
    img = Image.new("RGB", (320, 240))
    px = img.load()
    for i in range(320 * 240):
        v = raw[2 * i] | raw[2 * i + 1] << 8
        px[i % 320, i // 320] = ((v >> 11) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3)
    png = f[:-7] + ".png"
    img.resize((640, 480), Image.NEAREST).save(png)
    os.remove(f)
    print("wrote", png)
PY
