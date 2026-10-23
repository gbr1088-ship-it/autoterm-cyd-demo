# Project notes (read me first when resuming)

These are hand-over notes so a new session, or future me, can carry on without the original chat.
The README covers how to build and use the project. This file covers **where things stand, what was learned, and what's next**.

## Status
- **Working demo, tested on the real board.** It's flashed to an ESP32-2432S028 and runs well: splash, touch and the full UI.
- **The heater is still simulated.** Nothing talks to a real Autoterm yet.
- **DS18B20 cabin probe supported** on IO22. With no probe connected, the temperature is simulated. The code compiles, but it hasn't been tested with the user's probe yet.
- **Redesigned UI and real logo.** The redesign added the arc gauge, the round power button and the pill mode selector, plus the real Autoterm logo. Confirmed working on the board.
- **Probe not detected yet.** On the board the log shows `no DS18B20 on IO22 (bus presence pulse: no)`: nothing answers on the pin, so it's wiring (resistor, CN1 vs P3, data wire). Waiting on the user to check.
- **Home page + Utilities page (demo).** After the splash a home page picks Heating or Utilities. Utilities has light, water pump and bilge toggles and a hold-to-run windlass, all simulated. Tested on the board; the header home button was enlarged after the user found it too small.
- **Phone app (new, untested on the boards):** the screen hosts a phone web app over its own WiFi. Heater and switches can be controlled from it; the windlass is status-only. Details under "Phone app" below.
- **Repo:** `gbr1088-ship-it/autoterm-cyd-demo` (private). Pushes are done from the laptop.
- **Laptop setup (Windows):** a local Claude session now runs on the laptop and builds/flashes over USB (board on **COM4**, CH340). Python 3.12 is in `%LOCALAPPDATA%\Programs\Python\Python312`; run PlatformIO as `python -m platformio`. Git isn't on PATH: use GitHub Desktop's copy in `%LOCALAPPDATA%\GitHubDesktop\app-*\resources\app\git\cmd`.

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

- **This board's panel needs `TFT_INVERSION_ON`** (set in `platformio.ini`). Without it everything showed as a negative: white background, cyan instead of red.
- Touch uses `XPT2046_Touchscreen`, not TFT_eSPI's touch, because it's on a separate SPI bus.
- The backlight is driven by PWM (LEDC channel 0) for the fades.

## Code layout (`src/main.cpp`)
- **Gauge geometry:** `GX, GY, GR, GIR`. TFT_eSPI arc angles have 0° at 6 o'clock and run clockwise. The gauge sweeps 50°→310° for 0–35 °C.
- **Drawing:** dynamic text is drawn into sprites (`tempSpr`, `setSpr`, `infoSpr`, `anim`) to avoid flicker. **Sprite sizes and positions are chosen to stay inside the gauge ring.** Make them bigger and they erase parts of the arc.
- **Redraws:** `drawDynamic()` redraws only when a value changes.
- **`HEATER LINK` comments:** these mark where real heater commands go, in `onPowerPressed`, `onModePressed` and `onAdjust`. `simulate()` is the part to replace with real status polling.
- **Colours:** constants at the top. `C_RED` = Autoterm Pantone 185 red.
- **Pages:** `page` is `PAGE_HOME`, `PAGE_HEATER` or `PAGE_UTIL`; `showPage()` switches and redraws. Touch goes through `handleTouch()` → `onPress()`, which dispatches by page. The heater simulation runs on every page; drawing only happens for the visible one, and `setState()` only redraws heater widgets when the heater page is showing.
- **Home button:** `btnHome` covers the house icon *and* the logo (0,0 112×31) because the icon alone was too small for a finger. It stops above y = 32 where the utilities tiles start.
- **Utilities:** `toggles[]` (lights, water pump, bilge) and `winch` / `chainOut` for the windlass. `RELAY LINK` comments in `onToggle()` and `setWinch()` mark where relay-box commands go. `simulateWinch()` moves the chain counter (0.5 m/s, stops at 0 and 60 m).
- **Screensaver:** `PAGE_SAVER` starts after 60 s without a touch and shows a logo with the date and time. There are **two firmware versions** (PlatformIO environments `kolex`, the default, and `beneteau`; `pio run -e beneteau -t upload`) that differ only in the logo, and ready-made web-flasher files in `firmware/`: the white Beneteau logo on navy (`src/beneteau.h`, from beneteau.com's `logo-beneteau-white.svg` → `assets/beneteau_logo_white.png` via resvg-py → `tools/cutout2background.py`; white clock) and the Kolex logo (van-conversion firm, kolex-equipement.com) on a light gradient (`src/kolex.h`, from the site's `Kolex_JPG.jpg` → white removed with numpy into `assets/kolex_logo.png` → `tools/cutout2background.py`; dark clock). Both are trademarks, own use only. An earlier version used the user's van (`assets/screensaver.jpg`, cut out with rembg into `assets/van_cutout.png`); a tap returns to the previous page without pressing anything. `SAVER_IMAGES[]` can hold several images and alternates every 30 s. The clock starts at the firmware build time and resets on power-up (no RTC or internet time yet). Use `millis()` rather than the loop's `now` for the idle check, or the unsigned subtraction wraps and the screensaver fires on every touch.
- **Settings page:** reached from the SETTINGS bar at the bottom of the home page. Language (English / French / Spanish), brightness (10–100% in 10% steps; the screensaver runs at 60% of it) and screensaver on/off. Saved in NVS with `Preferences` (namespace `ui`), loaded in `setup()` before the splash.
- **Second screen (link):** the user has a second CYD (COM5 on the laptop; needs a USB-A→C or micro-USB cable, a laptop USB-C↔C cable gives it no power). Same firmware on both; Settings → LINK picks OFF / MAIN / REMOTE (French: NON / ECRAN 1 / ECRAN 2). ESP-NOW on channel 1: the main screen broadcasts a `StatePkt` every 250 ms (and straight after a remote's command); a remote draws from it, sends `CmdPkt` button presses back (power, mode, ±, toggles) and shows a red NO LINK after 2 s of silence. The windlass is main-screen only (greyed out on a remote). Pairing: MAIN opens a 60 s window (closes as soon as a remote joins), REMOTE follows the first main that's pairing; both are saved in NVS. Tested on both boards. The radio stack took the firmware from 48% to 84% of the app partition: switch to a bigger partition table (e.g. `huge_app.csv`, no OTA) before adding much more.
- **Over-the-air updates (working, tested on both boards 22 Oct 2026, v1 → v2 over the user's Livebox WiFi):**
  - **Publishing:** raise `FW_VERSION` in `platformio.ini`, then `powershell -ExecutionPolicy Bypass -File tools\publish_update.ps1`. It builds kolex + beneteau, copies each app image (`firmware.bin`, not the merged one) into a clone of the secret Gist (`..\autoterm_updates_gist`, id `ba27fb18aa7b7034e1fd2db4760486bf`), writes `version.txt` (`version=N` + `variant=<sha256> <size>` lines), pushes, and refreshes `firmware\` for USB. It refuses to publish a version that isn't higher. Run it from a normal terminal: Claude's sandboxed shell has its own copy of PlatformIO and can't push to the Gist without a sign-in prompt.
  - **On the screens:** Settings → SYSTEM page: link row, WiFi row (SET UP opens a WiFiManager portal `AUTOTERM-xxxx`, join from a phone, 192.168.4.1), version row (CHECK → INSTALL vN), info row (ID, free RAM, variant). Installs are refused while that screen runs the heater or windlass. The download is checked against GitHub's certificate (`src/github_roots.h`, from `tools/export_roots.ps1`) and the SHA-256 in version.txt.
  - **Safety net:** a new version is "on trial" (NVS namespace `ota`); if it doesn't stay up 30 s three times running, `checkNewFirmware()` switches back to the previous slot. Arduino's own app rollback isn't enabled in this toolchain, so this replaces it.
  - **Layout:** `min_spiffs.csv`, two 1.9 MB app slots (about 550 KB free); NVS is at the same place as before, so settings survived the switch.
  - **Clock:** connecting to WiFi sets the time by NTP (UK time zone, `TIMEZONE`); it's still lost at power-off until the next WiFi use.
- **Phone app (written 23 Oct 2026, compiled but not yet tried on the boards):**
  - **What:** home page → PHONE → ON opens a WPA2 access point `AVL-xxxx` (from the MAC, like `AUTOTERM-xxxx`) on `RADIO_CHANNEL`, so ESP-NOW keeps working alongside it. The phone page shows a WiFi-join QR code (the SDK's `esp_qrcode_*`, no extra library), the password (8 characters, made with `esp_random()` the first time, kept in NVS `ui/phonepw`) and `192.168.4.1`. NEW PASSWORD makes a fresh one and drops every phone. ON/OFF is kept in NVS `ui/phone`, off by default.
  - **How:** `WebServer` on port 80 serves the gzipped page from `src/webapp.h`. `WebSocketsServer` (links2004/WebSockets 2.4.1) runs on port 81: the screen broadcasts its state as JSON every 250 ms and straight after a command (`phoneSendState()`), and the phone sends `power`, `mode N`, `adj ±1` or `toggle N`. These go through the same `onPowerPressed()` / `onModePressed()` / `onAdjust()` / `onToggle()` as the screen's own buttons, so every rule is the same. **No windlass from the phone**, for the same reason as on a remote.
  - **Radio:** `phoneUp` means the AP is up. `wifiOff()` now puts the phone AP back, and `radioBegin()` leaves the mode alone while it's up. The WiFiManager setup portal needs port 80 and the AP, so `startWifiSetup()` closes the phone network and `stopWifiSetup()` reopens it. Update checks briefly move the radio to the boat network's channel, and phones reconnect afterwards. A remote screen never hosts the app (`phoneAllowed()`).
  - **The page:** `web/app.html` holds only the page's content. `tools/web2header.py` wraps it in a full document (home-screen icon `web/icon.png`, manifest), gzips it (~11 KB) and writes `src/webapp.h`. Opened anywhere but from the screen, the page runs a JS copy of `simulate()` as a demo. The firmware grew by about 47 KB (72% of the slot).
  - **To check on the boards:** the QR code scans, both screens keep their link while a phone is connected, the WiFi setup still works with the phone app on, and the labels fit (they were measured against the font tables, but not seen on the screen).
- **Adventure Van Life branding (23 Oct 2026, replaces the Autoterm branding; the heater page keeps Autoterm):** the user's own logo, `assets/avl_logo_original.jpg` (black lettering on an orange glow, 1000×1000, background #FF914C with a lighter #FCB58B centre). `tools/avl_logo.py` lifts the lettering out as a mask and writes: `assets/avl_logo.png` (the tile, splash 150×150), `assets/avl_logo_small.png` (the lettering in orange for the dark header, 31×24; a 22 px orange tile was unreadable), `assets/avl_ink.png` (screensaver), `web/icon.png`, and the tile as a data URI in `web/app.html`. `src/avl_logo.h` comes from png2rgb565 `--name avl_logo`. `drawHeaderLogo()` uses it on every page except `PAGE_HEATER`. New PlatformIO env **`avl`** (now the default, and in `publish_update.ps1`) has the `src/avl.h` screensaver: the logo on an orange gradient, with a dark clock. A screen on kolex only downloads kolex updates, so switch a screen to avl once over USB. Phone app: titled Adventure Van Life with the tile, "Autoterm heater" above the gauge, orange selected tab, network `AVL-xxxx`, home-screen name "Van Life". An earlier logo Claude designed (badge + Barlow wordmark) was dropped when the user supplied this one.
- **Translations:** every on-screen string lives in `TXT[lang][Txt]` (plus `DAY_NAME` / `MONTH_NAME` for the screensaver date); use `tr(T_...)` for screen text and `trEn()` for the serial log. The fonts are ASCII only, so French and Spanish are written without accents. Labels were shortened to fit their boxes (e.g. "POMPE CALE", "SENTINA"); check the widths when adding text.
- **Touch hold:** the XPT2046 reading drops out briefly while a finger is held, so gaps under 150 ms count as still held. Without this the windlass would stutter.
- **Probe reading:** `pollSensor()` reads the DS18B20 without blocking. It starts a conversion and collects it a second later. It searches again every 5 s, and after 3 failed reads it falls back to the simulation. `simulate()` only changes `cabinTemp` while `sensorOk` is false.

## Logo
- **Source:** Autoterm's logo/brand PDF (4 pages). Page 1 = logo with tagline, page 2 = logo without tagline.
- **Render:** `pdftocairo -png -transp -r 600 -f 1 -l 2 logo.pdf hi`, then crop the top 35% of each page to the alpha bounding box. The results are in `assets/`.
- **Convert:** `python tools/png2rgb565.py assets/autoterm_logo.png --small-image assets/autoterm_logo_plain.png` writes `src/logo.h`. The splash logo is 280×86 and the header logo 72×22.
- **Trademark:** the logo is Autoterm's. Fine on your own device; ask Autoterm before sharing or selling anything that uses it.

## Building
1. **Normal:** PlatformIO (`pio run -t upload`). Versions are pinned: espressif32 6.9.0 (Arduino core 2.0.17), TFT_eSPI 2.5.43, XPT2046_Touchscreen 1.4, OneWire 2.3.8, DallasTemperature 3.9.0. DallasTemperature 3.9.0 is no longer in the PlatformIO registry, so `platformio.ini` fetches it from its GitHub tag (needs git).
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

## Utilities: relay box plan (agreed with the user, not built yet)
The screen will also control lights, water pump, bilge pump and the anchor windlass on a boat, through a separate wireless relay box.
- **Link:** ESP-NOW between the CYD and an ESP32 relay board. No router needed, a few ms latency, good range on a boat. Bluetooth and WiFi were considered and rejected (pairing hassle / needs a router).
- **Relay board:** an ESP32 relay board that runs straight from 12 V, e.g. Kincony KC868-A4/A8, or a generic "ESP32 4-channel relay board, 7–30 V input". Not bought yet.
- **Wiring:** the board's ~10 A relays are control switches only.
  - Lights: direct if under ~5 A, otherwise via an automotive relay.
  - Water pump: via a fused automotive 30/40 A relay.
  - Bilge pump: relay **in parallel with the float switch**, so the screen can only add a manual run. The automatic float circuit must never depend on the screen or the radio.
  - Windlass: switch the coils of its existing up/down solenoid or contactor, never the motor (100 A+).
- **Safety rules to build into both ends:**
  1. Windlass is hold-to-run. The screen sends a keep-alive every 100 ms while a button is held; the relay box stops by itself after ~300 ms without one.
  2. The relay box never energises UP and DOWN together (enforced on the box, not just the screen).
  3. The original windlass switches / foot switches stay fitted.
  4. Relay box powers up with everything off, and turns everything except the bilge off if it loses contact with the screen.
  5. Fuse every circuit; sealed enclosure.
- **Screen side:** show a relay as on only after the box acknowledges it.

## Next steps
1. Fix the probe wiring with the user, then confirm "Probe OK" on the screen.
2. Write `heater.cpp/.h`, the protocol driver:
   - frame builder and parser, CRC
   - status polling about once a second
   - start, stop and setting commands
3. Add a demo/real switch, such as a long-press on the logo or a compile flag, so the simulation stays available.
4. Show heater error codes on the screen.
5. Bench test with the heater, with the original panel kept as a fallback.
6. Try the phone app on both boards (see the checklist under "Phone app").
7. Once the user picks a relay board: write its ESP-NOW firmware and replace the `RELAY LINK` stubs with real commands, keep-alive and acknowledgements.

**Safety:** never cut power to stop the heater, because it must run its cool-down purge. Fit a CO alarm in any heated space.
