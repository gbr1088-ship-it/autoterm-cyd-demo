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

## Screens
- **Splash**: logo and a loading bar.
- **Main**:
  - Header: state and supply voltage.
  - Left card: cabin temperature, fan rpm and heat output.
  - Right card: the setpoint, with − / + buttons (hold to repeat).
  - Modes: **TEMP** (1–30 °C), **POWER** (level 0–9), **VENT** (fan only, 1–9).
  - **START / STOP** button, plus an animated flame or fan.
- States run OFF → STARTING (12 s glow plug) → HEATING → COOLING DOWN (10 s purge) → OFF.
- The on-board RGB LED is red while heating and blue while ventilating or cooling down.

## Using the real Autoterm logo
```
pip install pillow
python tools/png2rgb565.py path/to/autoterm_logo.png
```
This writes `src/logo.h`, and the splash screen uses it automatically on the next build. Until then a drawn stand-in (flame + "AUTOTERM" wordmark) is shown.

## Troubleshooting
| Symptom | Fix (in `platformio.ini` / `main.cpp`) |
|---|---|
| Garbled or mirrored image | Change `ILI9341_2_DRIVER` to `ILI9341_DRIVER`, or `ST7789_DRIVER` on 2-USB-port boards |
| Colours inverted | Add `-D TFT_INVERSION_ON=1` (or `_OFF`) |
| Screen upside down | `tft.setRotation(3)` **and** `ts.setRotation(3)` |
| Taps land in the wrong place | The serial monitor prints raw touch values; adjust `TOUCH_X/Y_MIN/MAX`. If an axis is reversed, swap that axis's MIN and MAX |

## Next step: a real heater
Search for `HEATER LINK` in `main.cpp`. Those are the places where start, stop, mode and setpoint commands go. Autoterm heaters talk over a 2400 baud UART on the control-panel connector, so they can be wired to the CYD's spare UART pins (via the P1/CN1 header) through a level shifter.
