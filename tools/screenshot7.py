#!/usr/bin/env python3
"""Save a screenshot of the 7" board (env avl7) over its USB socket.

    pip install pyserial pillow
    python tools/screenshot7.py COM7 shot.png

The firmware answers an 'S' with "SHOT <w> <h>" and the raw RGB565 pixels
(see display7Loop in src/display7.h). The picture is what the firmware drew,
before the panel's colour correction.
"""
import sys
import time

import serial
from PIL import Image

port, out = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "shot.png"
# Open without raising DTR/RTS: on the ESP32-S3's own USB port those reset
# the chip (into its bootloader if DTR is up).
s = serial.Serial()
s.port, s.baudrate, s.timeout = port, 115200, 5
s.dtr = s.rts = False
s.open()
s.reset_input_buffer()
s.write(b"S")
deadline = time.time() + 10
while True:
    line = s.readline()
    if line.startswith(b"SHOT "):
        break
    if time.time() > deadline:
        sys.exit("no answer: is the avl7 firmware running on " + port + "?")
w, h = (int(v) for v in line.split()[1:3])
raw = s.read(w * h * 2)
if len(raw) != w * h * 2:
    sys.exit(f"only got {len(raw)} of {w * h * 2} bytes")
px = bytearray()
for i in range(0, len(raw), 2):
    c = (raw[i] << 8) | raw[i + 1]
    px += bytes((((c >> 11) & 0x1F) * 255 // 31, ((c >> 5) & 0x3F) * 255 // 63, (c & 0x1F) * 255 // 31))
Image.frombytes("RGB", (w, h), bytes(px)).save(out)
print("saved", out)
