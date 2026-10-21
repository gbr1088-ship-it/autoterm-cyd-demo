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
- **Splash**: the Autoterm logo fades in, followed by a loading bar.
- **Main**:
  - Header: the small Autoterm logo, a coloured state pill and the supply voltage.
  - Gauge: a curved cabin-temperature gauge that sweeps from blue to red. In TEMP mode it also shows a setpoint marker.
  - Inside the gauge: the temperature, the setpoint, and an animated flame or fan. Fan rpm and heat output sit underneath.
  - Buttons: + and − (hold to repeat), and a round power button that glows green when stopped, red when running and grey while cooling down.
  - Mode switch: **TEMP** (1–30 °C), **POWER** (level 0–9) and **VENT** (fan only, 1–9).
- States run OFF → STARTING (12 s glow plug) → HEATING → COOLING DOWN (10 s purge) → OFF.
- The on-board RGB LED is red while heating and blue while ventilating or cooling down.

## Logo
`src/logo.h` is generated from the artwork in `assets/`, which was taken from Autoterm's logo PDF. To regenerate it, or to swap in other artwork:
```
pip install pillow
python tools/png2rgb565.py assets/autoterm_logo.png --small-image assets/autoterm_logo_plain.png
```
If you delete `src/logo.h`, the firmware falls back to a drawn stand-in logo.

## Troubleshooting
| Symptom | Fix (in `platformio.ini` / `main.cpp`) |
|---|---|
| Garbled or mirrored image | Change `ILI9341_2_DRIVER` to `ILI9341_DRIVER`, or `ST7789_DRIVER` on 2-USB-port boards |
| Colours inverted | Add `-D TFT_INVERSION_ON=1` (or `_OFF`) |
| Screen upside down | `tft.setRotation(3)` **and** `ts.setRotation(3)` |
| Taps land in the wrong place | The serial monitor prints raw touch values; adjust `TOUCH_X/Y_MIN/MAX`. If an axis is reversed, swap that axis's MIN and MAX |

## Next step: a real heater
Search for `HEATER LINK` in `main.cpp`. Those are the places where start, stop, mode and setpoint commands go. Autoterm heaters talk over a 2400 baud UART on the control-panel connector, so they can be wired to the CYD's spare UART pins (via the P1/CN1 header) through a level shifter.
