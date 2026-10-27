// Lomax dashboard: the settings you are most likely to change.
// Wiring diagrams and the reasons behind these values are in LOMAX.md.
#pragma once

// ---------------------------------------------------------------- pins
// The CYD has three free pins on its connectors (IO35 on P3, IO22 and IO27
// on CN1). The other three lamp inputs use the SD card slot's pins through
// a micro SD "sniffer" breakout plugged into the slot (no SD card is used).
#define GPS_RX_PIN   35  // P3: the GPS module's TX. Input only, which suits it
#define TACHO_PIN    22  // CN1: optocoupler output from the coil's negative
#define IND_L_PIN    27  // CN1: left indicator lamp (or the only one)
#define IND_R_PIN    18  // SD slot: right indicator. -1 if there is one lamp
#define BEAM_PIN     19  // SD slot: main beam
#define SIDE_PIN     23  // SD slot: sidelights

// The lamp inputs read LOW when the lamp is lit, which is what an
// optocoupler pulling the pin to ground gives. Set to 0 for active-high.
#define LAMPS_ACTIVE_LOW 1

// ---------------------------------------------------------------- GPS
#define GPS_BAUD 9600         // NEO-6M / NEO-M8N default
// Optional: the GPS module's RX on a spare pin lets the dash switch a u-blox
// GPS to 5 updates a second at start-up, for a livelier speedo. With one
// indicator lamp (IND_R_PIN -1) IO18 is free for it.
#define GPS_TX_PIN   -1
#define GPS_MIN_MPH 1.5f      // below this the car is treated as standing still
                              // (a parked GPS wanders a little)
#define GPS_TIMEOUT_MS 3000   // no fix for this long: speed shows 0, GPS lamp lit

// Local time from the GPS's UTC, as a POSIX TZ string.
// UK: GMT in winter, BST (+1) from the last Sunday of March to the last of October.
#define LOCAL_TZ "GMT0BST,M3.5.0/1,M10.5.0"

// ---------------------------------------------------------------- tacho
// A 2CV's single coil fires both plugs at once (wasted spark), once every
// crank revolution, so one pulse per rev. An alternator W terminal gives
// (number of poles / 2) x pulley ratio pulses per rev instead.
#define TACHO_PULSES_PER_REV 1.0f
#define TACHO_TIMEOUT_MS 500  // no pulse for this long: engine stopped
#define TACHO_MAX_RPM 7000    // pulses faster than 1.25 x this are ignition ringing

// ---------------------------------------------------------------- dials
#define SPEEDO_MAX 90         // mph at full scale (multiple of 10)
#define SPEEDO_KMH_SCALE 1    // 1: inner km/h scale on the big speedo
#define TACHO_MAX 7           // x1000 rpm at full scale
#define TACHO_RED_FROM 5.5f   // x1000 rpm where the red band starts

// ---------------------------------------------------------------- screen
#define BRIGHT_DAY   255      // backlight 0-255
#define BRIGHT_NIGHT 70       // with the sidelights on
#define LONG_PRESS_MS 2000    // hold this long to reset the trip
#define SPLASH_MS    2500     // start-up logo
#define SHOW_LAMP_CHECK 1     // 1: all lamps and a needle sweep after the logo

// Touch calibration (as the heater firmware; see README "Troubleshooting")
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3700
#define TOUCH_Y_MIN 240
#define TOUCH_Y_MAX 3800
