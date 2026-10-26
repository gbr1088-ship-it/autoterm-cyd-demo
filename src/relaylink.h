// ESP-NOW packets between a screen and the wireless relay box (relaybox.cpp).
// Shared by both firmwares, so a change here needs both rebuilt.
//
// The screen sends what it wants every 250 ms, every 100 ms while the
// windlass runs, and straight away on a change. The box answers each one
// with what it is actually doing, and the screen shows a relay as on only
// once the box says so. While it has no screen the box broadcasts its state
// with `pairing` set; the first main screen that hears it joins. The box
// always broadcasts and names its screen, so any other screen that had
// paired with it can tell it has been taken and go back to demo relays.
#pragma once
#include <stdint.h>

const uint32_t BOX_MAGIC = 0x31425441;  // "ATB1"; change if the packets change
enum BoxPktType : uint8_t { BOX_CMD = 10, BOX_STATE = 11 };

// bits of `outputs`: one per output DO1..DO8 on the box
enum BoxOut : uint8_t {
  OUT_LIGHTS = 0,       // DO1
  OUT_WATER_PUMP = 1,   // DO2
  OUT_BILGE_PUMP = 2,   // DO3, manual run in parallel with the float switch
  OUT_WINCH_UP = 3,     // DO4, only through `winch`, never through `outputs`
  OUT_WINCH_DOWN = 4,   // DO5, likewise
  // DO6..DO8 spare
};
const uint8_t WINCH_BITS = (1 << OUT_WINCH_UP) | (1 << OUT_WINCH_DOWN);
enum BoxWinch : uint8_t { BOXW_STOP, BOXW_UP, BOXW_DOWN };  // as the screen's Winch

const uint32_t BOX_WINCH_HOLD_MS = 300;   // windlass stops without a keep-alive this long
const uint32_t BOX_LOST_MS = 10000;       // then everything but the bilge goes off

struct __attribute__((packed)) BoxCmdPkt {
  uint32_t magic;
  uint8_t type;     // BOX_CMD
  uint8_t join;     // 1: asking a pairing box to take this screen
  uint8_t outputs;  // wanted on; the windlass bits are ignored
  uint8_t winch;    // BoxWinch, held while the button is
};

struct __attribute__((packed)) BoxStatePkt {
  uint32_t magic;
  uint8_t type;     // BOX_STATE
  uint8_t pairing;  // 1: no screen yet, will take the first that joins
  uint8_t outputs;  // what is on now, windlass bits included
  uint8_t inputs;   // DI1..DI8, 1 = closed
  uint8_t ok;       // 0: the output chip didn't answer, nothing can switch
  uint32_t uptime;  // seconds
  uint8_t screen[6];  // the screen it obeys, when not pairing
};
