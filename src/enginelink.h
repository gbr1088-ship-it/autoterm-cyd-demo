// ESP-NOW packets from the Lomax engine and lights box (enginebox.cpp) to the
// dash (lomax.cpp). Shared by both firmwares, so a change here needs both
// rebuilt.
//
// The box broadcasts one EnginePkt every 100 ms, and straight away when a
// lamp changes, whether or not a dash is listening: the lights never wait
// for the screen. A dash takes the first box it hears and from then on only
// listens to that one (its address is kept), so another Lomax parked next to
// it can't move its needles. Type "unpair" on the dash's serial port to let
// it take a different box.
#pragma once
#include <stdint.h>

const uint32_t ENGINE_MAGIC = 0x31424C45;  // "ELB1"; change if the packet changes
const uint8_t ENGINE_CHANNEL = 1;          // WiFi channel; the dash's update network uses it too
const uint32_t ENGINE_EVERY_MS = 100;
const uint32_t ENGINE_LOST_MS = 1000;      // the dash shows "--" after this long without a packet

// bits of `valid`: which readings can be trusted (the sensor is fitted and
// answering, and its reading is in range)
enum EngineValid : uint16_t {
  EV_OIL_PSI = 1 << 0,
  EV_OIL_TEMP = 1 << 1,
  EV_CHT_L = 1 << 2,
  EV_CHT_R = 1 << 3,
  EV_RPM = 1 << 4,    // the box has the coil's pulses (TACHO_FITTED)
  EV_WHEEL = 1 << 5,  // a wheel speed sensor is fitted
  EV_BATT = 1 << 6,
};

// bits of `lights`: what is lit (the indicators flash in these bits) and
// which switches are on
enum EngineLights : uint16_t {
  EL_IND_L = 1 << 0,    // left indicators lit right now
  EL_IND_R = 1 << 1,
  EL_HAZARD = 1 << 2,   // hazard switch on
  EL_SIDE = 1 << 3,     // sidelights on
  EL_BEAM = 1 << 4,     // main beam switch (read only, the box doesn't switch it)
  EL_BRAKE = 1 << 5,    // brake pedal switch
  EL_DRL = 1 << 6,      // daytime running lights on
  EL_IGN = 1 << 7,      // ignition on
  EL_OIL_LOW = 1 << 8,  // the engine's own oil pressure switch says low
};

struct __attribute__((packed)) EnginePkt {
  uint32_t magic;
  uint8_t seq;          // counts up, so a repeat can be spotted
  uint8_t spareIn;      // the spare inputs, 1 = closed
  uint16_t valid;       // EngineValid
  uint16_t lights;      // EngineLights
  uint8_t outputs;      // DO1..DO8 as they are now
  uint8_t inputs;       // DI1..DI8, 1 = 12 V there
  int16_t oilPsi10;     // tenths of a psi
  int16_t oilTemp10;    // tenths of a degree C
  int16_t chtL10, chtR10;
  uint16_t rpm;
  uint16_t wheelMph10;  // tenths of a mph
  uint16_t battMv;      // supply voltage, mV
  uint16_t spareMv;     // the spare analogue input, mV at the box's terminal
  uint32_t uptime;      // seconds
};
