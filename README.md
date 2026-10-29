# Autoterm heater touchscreen demo (ESP32-2432S028 / "Cheap Yellow Display")

This is a demo controller UI for an Autoterm (Planar) diesel heater. The heater is **simulated**, so the screen, touch, animation and a rough cabin-heating model all work with nothing else connected.

## Build & flash
1. Install VS Code + the PlatformIO extension (or `pip install platformio`).
2. Open this folder, plug in the board over USB, then:
   ```
   pio run -t upload
   pio device monitor
   ```
   If the upload won't start, hold **BOOT** and tap **RST**.

### 7 inch screen (Waveshare ESP32-S3-Touch-LCD-7)
The same Adventure Van Life firmware also builds for Waveshare's 7" ESP32-S3 board:
```
pio run -e avl7 -t upload
```
Plug into the port marked **UART**. It shows the same screens at double size
(640x480 in the middle of the 800x480 panel, the side margins filled in), with
the touch scaled to match. Its backlight is on/off only, so the brightness
setting doesn't dim it. The DS18B20 probe goes on **IO6** (the Sensor header).

### Lomax dashboard
The same CYD board can also be a car dashboard: a GPS speedo, a rev counter
from the coil, warning lamps and an old style clock. It's a separate firmware,
`pio run -e lomax -t upload`. It also runs on Waveshare's round 1.28" ESP32-S3
screen (`-e lomax_round`) and, landscape, on the 7" (`-e lomax7`). See [LOMAX.md](LOMAX.md).

## Screens
- **Splash**: the Adventure Van Life logo fades in, followed by a loading bar.
- **Main**:
  - Header: the word AUTOTERM, a coloured state pill and the supply voltage.
  - Gauge: a curved cabin-temperature gauge that sweeps from blue to red. In TEMP mode it also shows a setpoint marker.
  - Inside the gauge: the temperature, the setpoint, and an animated flame or fan. Fan rpm and heat output sit underneath.
  - Buttons: + and − (hold to repeat), and a round power button that glows green when stopped, red when running and grey while cooling down.
  - Mode switch: **TEMP** (1–30 °C), **POWER** (level 0–9) and **VENT** (fan only, 1–9).
- States run OFF → STARTING (12 s glow plug) → HEATING → COOLING DOWN (10 s purge) → OFF.
- The on-board RGB LED is red while heating and blue while ventilating or cooling down.

## Phone app
Tap **PHONE** on the home page and switch it **ON**. The screen opens its own WiFi network (`AVL-xxxx`, with a password) and shows a QR code for joining it.
1. Scan the QR code with the phone's camera, or join `AVL-xxxx` by hand with the password on the screen.
2. Open **http://192.168.4.1** in Safari or Chrome. Then use *Add to Home Screen* (Share menu on iPhone, ⋮ menu on Android) and it opens like an app from then on.

The app shows the cabin gauge and heater state, and it can start and stop the heater, change mode and setpoint, and switch the lights, water pump and manual bilge run. It follows the screen's language. The windlass shows its chain count but runs only from the main screen, where you can see the bow.

- No internet or router is needed, and phones may say the network has no internet. That's expected: stay connected.
- **NEW PASSWORD** on the phone page drops every phone that knew the old one.
- Only the main screen (or a screen with LINK set to OFF) hosts the app. The network shares the ESP-NOW channel, so a second screen keeps working.
- The app lives in `web/app.html`. After editing it, run `python tools/web2header.py` to rebuild `src/webapp.h`. Opened straight from disk, the page runs a demo with a simulated heater.
- **Demo to send to people:** `demo/adventure-van-life-demo.html` is the whole app in one file (also rebuilt by `web2header.py`). It opens in any browser and runs with a simulated heater. To share it as a link, upload it to a static host, e.g. drag it onto https://app.netlify.com/drop.

### Updating the firmware from a browser
With the phone app on, a phone or laptop on the `AVL-xxxx` network can install new firmware without USB or internet:
1. Build it (`pio run -e avl`, or the screen's own version) and copy `.pio/build/<version>/firmware.bin` to the phone or laptop. Use `firmware.bin`, not the merged USB file.
2. Open **http://192.168.4.1/update**, choose the file and tap **Install**. The screen shows a progress bar and restarts when it's done.

Turn the heater and windlass off first, or the update is refused. A file for the other kind of board (CYD vs 7") is refused too. As with the downloaded updates, a new version that keeps crashing in its first 30 s is swapped back for the previous one.

## Adventure Van Life branding
The splash screen, the header on every page but the heater's, the phone app and the `avl` screensaver use the Adventure Van Life logo. The heater page's header says AUTOTERM in plain text. The artwork is `assets/avl_logo_original.jpg`. `tools/avl_logo.py` makes every copy from it: the orange tile for the splash and the phone, the lettering in orange for the dark header bar, and the screensaver. To change the logo, replace that file, run the script, then run the three commands it prints.

Firmware versions (they differ only in the screensaver): `pio run -e avl -t upload` (default), `-e kolex`, `-e beneteau`.

## No Autoterm artwork
Autoterm's logo is copyrighted, so the firmware and the phone app don't contain it. Only the name "Autoterm" appears, as plain text on the heater page.

## Cabin temperature sensor (DS18B20)
Plug a DS18B20 probe into the **CN1** header and the gauge shows the real temperature. With no probe connected, the temperature is simulated. The board checks for the probe every 5 s, so you can plug it in while it's running.

```
DS18B20 red    ──┬── 3.3V  (CN1)
              [4.7kΩ]
DS18B20 yellow ──┴── IO22  (CN1)
DS18B20 black  ───── GND   (CN1)
```
The 4.7 kΩ resistor between data and 3.3V is required. Mount the probe away from the heater outlet and out of direct sun.

## Troubleshooting
| Symptom | Fix (in `platformio.ini` / `main.cpp`) |
|---|---|
| Garbled or mirrored image | Change `ILI9341_2_DRIVER` to `ILI9341_DRIVER`, or `ST7789_DRIVER` on 2-USB-port boards |
| Colours inverted | Add `-D TFT_INVERSION_ON=1` (or `_OFF`) |
| Screen upside down | `tft.setRotation(3)` **and** `ts.setRotation(3)` |
| Taps land in the wrong place | The serial monitor prints raw touch values; adjust `TOUCH_X/Y_MIN/MAX`. If an axis is reversed, swap that axis's MIN and MAX |

## Next step: a real heater
Search for `HEATER LINK` in `main.cpp`. Those are the places where start, stop, mode and setpoint commands go.

Autoterm heaters (AIR 2D/4D) talk over a 5 V TTL UART on the control-panel harness at 9600 baud, 8N1. The community reverse-engineered the protocol: [schroeder-robert/autoterm-air-2d-serial-control](https://github.com/schroeder-robert/autoterm-air-2d-serial-control). Wire it through a 5 V ↔ 3.3 V level shifter: TX = **GPIO27** (CN1) and RX = **GPIO35** (P3, input only, which suits a receive line). GPIO22 is taken by the temperature sensor. Never connect the harness's +12 V wire to a signal pin.

