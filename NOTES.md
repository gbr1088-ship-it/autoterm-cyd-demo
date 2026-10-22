# Project notes (read me first when resuming)

These are hand-over notes so a new session, or future me, can carry on without the original chat.
The README covers how to build and use the project. This file covers **where things stand, what was learned, and what's next**.

## Status
- **Working demo, tested on the real board.** It's flashed to an ESP32-2432S028 and runs well: splash, touch and the full UI.
- **The heater is still simulated.** Nothing talks to a real Autoterm yet.
- **DS18B20 cabin probe supported** on IO22. With no probe connected, the temperature is simulated. The code compiles, but it hasn't been tested with the user's probe yet.
- **Redesigned UI and real logo.** The redesign added the arc gauge, the round power button and the pill mode selector, plus the real Autoterm logo. It compiles cleanly. *Confirm on the board that it looks right.*
- **Repo:** `gbr1088-ship-it/autoterm-cyd-demo` (private). Pushes are done from the laptop.

## Hardware: ESP32-2432S028 ("Cheap Yellow Display")
| Function | Pins |
|---|---|
| Display ILI9341 (HSPI) | MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, BL 21 |
| Touch XPT2046 (own VSPI bus) | CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36 |
| RGB LED (active LOW) | R 4, G 16, B 17 |
| CN1 header | GND / IO22 / IO27 / 3.3V |
| P3 header | GND / IO35 / IO22 / IO21 (IO21 = backlight, don't use) |
| **Pin plan** | **IO22 = DS18B20 data, IO27 = heater TX, IO35 = heater RX** (input only, fine for RX) |
| Not usable | IO1/IO3 (USB serial) |

- Touch uses `XPT2046_Touchscreen`, not TFT_eSPI's touch, because it's on a separate SPI bus.
- The backlight is driven by PWM (LEDC channel 0) for the fades.

## Code layout (`src/main.cpp`)
- **Gauge geometry:** `GX, GY, GR, GIR`. TFT_eSPI arc angles have 0° at 6 o'clock and run clockwise. The gauge sweeps 50°→310° for 0–35 °C.
- **Drawing:** dynamic text is drawn into sprites (`tempSpr`, `setSpr`, `infoSpr`, `anim`) to avoid flicker. **Sprite sizes and positions are chosen to stay inside the gauge ring.** Make them bigger and they erase parts of the arc.
- **Redraws:** `drawDynamic()` redraws only when a value changes.
- **`HEATER LINK` comments:** these mark where real heater commands go, in `onPowerPressed`, `onModePressed` and `onAdjust`. `simulate()` is the part to replace with real status polling.
- **Colours:** constants at the top. `C_RED` = Autoterm Pantone 185 red.
- **Probe reading:** `pollSensor()` reads the DS18B20 without blocking. It starts a conversion and collects it a second later. It searches again every 5 s, and after 3 failed reads it falls back to the simulation. `simulate()` only changes `cabinTemp` while `sensorOk` is false.

## Logo
- **Source:** Autoterm's logo/brand PDF (4 pages). Page 1 = logo with tagline, page 2 = logo without tagline.
- **Render:** `pdftocairo -png -transp -r 600 -f 1 -l 2 logo.pdf hi`, then crop the top 35% of each page to the alpha bounding box. The results are in `assets/`.
- **Convert:** `python tools/png2rgb565.py assets/autoterm_logo.png --small-image assets/autoterm_logo_plain.png` writes `src/logo.h`. The splash logo is 280×86 and the header logo 72×22.
- **Trademark:** the logo is Autoterm's. Fine on your own device; ask Autoterm before sharing or selling anything that uses it.

## Building
1. **Normal:** PlatformIO (`pio run -t upload`). Versions are pinned: espressif32 6.9.0 (Arduino core 2.0.17), TFT_eSPI 2.5.43, XPT2046_Touchscreen 1.4, OneWire 2.3.8, DallasTemperature 3.9.0.
2. **If the PlatformIO registry is blocked** (it was in the cloud session): use arduino-cli 1.1.1 from GitHub releases.
   - Clone `espressif/arduino-esp32` at tag `2.0.17` into `<user>/hardware/espressif/esp32`, then run its `tools/get.py`.
   - Clone the four libraries (TFT_eSPI, XPT2046_Touchscreen, OneWire and DallasTemperature, at the versions above) into `<user>/libraries`. DallasTemperature lives at `milesburton/Arduino-Temperature-Control-Library`, tag `3.9.0`.
   - Put `main.cpp` in a sketch folder next to an empty `autoterm.ino`.
   - Pass the `-D` flags from `platformio.ini` via `--build-property compiler.cpp.extra_flags=...` (and `compiler.c.extra_flags`).
   - arduino-cli needs ctags, which wasn't downloadable. A stub script that just `exit 0`s works, passed via `runtime.tools.ctags.path`.
   - Build with FQBN `espressif:esp32:esp32`.
3. **Single file for the browser flasher:**
   ```
   esptool.py --chip esp32 merge_bin -o firmware.bin --flash_mode dio --flash_freq keep --flash_size 4MB \
     0x1000 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 app.bin
   ```
   Flash it at **0x0** with https://espressif.github.io/esptool-js/ (Chrome or Edge).

## Real heater: findings so far
**Protocol** (AIR 2D/4D, community reverse-engineered: <https://github.com/schroeder-robert/autoterm-air-2d-serial-control>):
- **Serial link:** 9600 baud, 8N1, 5 V TTL. (An earlier README said 2400; that was wrong.)
- **Frame:** `AA <dir> <len> 00 <type> <payload…> <crc_hi> <crc_lo>`. `dir` = `03` panel→heater, `04` heater→panel.
- **CRC:** CRC-16/MODBUS (init 0xFFFF, poly 0xA001) over the header and payload, **high byte first**. Checked against the example messages below.
- **Example messages** from that repo, not yet verified on our heater:
  - **Turn on (type 01):** `AA 03 06 00 01 01 00 04 10 00 08 B3 EE` → reply `AA 04 06 00 01 01 00 04 10 00 08 69 5F`
  - **Turn off (type 03):** `AA 03 00 00 03 5D 7C` → reply `AA 04 00 00 03 29 7D`
  - **Get status (type 0F):** `AA 03 00 00 0F 58 7C` → reply `AA 04 13 00 0F 00 01 00 13 7F 00 86 01 24 00 00 00 00 00 00 00 00 00 64 D9 49`
- **Caveat:** the protocol documentation is incomplete. The payload meanings (mode, setpoint, power level and status fields) still need decoding and testing.
- **Other references:**
  - <https://github.com/sinise/autoterm-heater-control> (wiring notes, AIR 5D)
  - <https://github.com/alexsanzder/dbus-autoterm>
  - Pekaway and Arduino forum threads (German)

**Parts to add:**
- 12 V→5 V buck converter (1 A or more) to power the CYD
- BSS138 level shifter (5 V ↔ 3.3 V)
- spare Autoterm panel cable or connector
- 1–2 A inline fuse
- JST 1.25 mm 4-pin lead for CN1
- DS18B20 cabin temperature probe (the user has one), with a 4.7 kΩ pull-up resistor

**Wiring plan** (the CYD replaces the original panel):
- **Data:** heater white (panel→heater) ← shifter ← GPIO27 (TX). Heater yellow (heater→panel) → shifter → GPIO35 (RX).
- **Ground:** shared with the heater. This is essential, or the data comes through as garbage.
- **Red +12 V:** never connect it to a signal pin.
- **Check first:** wire colours vary between models. Measure with a multimeter: the data lines idle at about 5 V, and the power wire reads 12 V.

**Open questions:**
- Which wire is which on this particular heater's harness?
- In TEMP mode, does the heater rely on the panel's built-in temperature sensor? If so, the CYD must send its own reading.
- Can the original panel stay connected alongside the CYD? That needs two UARTs. The current plan is to replace the panel instead.

## Next steps
1. Confirm the redesigned UI looks good on the board, and tweak it if needed.
2. Write `heater.cpp/.h`, the protocol driver:
   - frame builder and parser, CRC
   - status polling about once a second
   - start, stop and setting commands
3. Add a demo/real switch, such as a long-press on the logo or a compile flag, so the simulation stays available.
4. Show heater error codes on the screen.
5. Bench test with the heater, with the original panel kept as a fallback.

**Safety:** never cut power to stop the heater, because it must run its cool-down purge. Fit a CO alarm in any heated space.
