// Engine and lights box for the Lomax dash
// Target: an ESP32-S3-DevKitC-1 on a carrier board, built with `pio run -e enginebox`.
// Wiring, parts and the reasons behind it are in ENGINEBOX.md.
//
// Reads the engine's sensors and sends them to the dash over ESP-NOW
// (enginelink.h), and switches the lights from the car's own switches:
//   oil pressure (5 V transducer) and oil temperature (NTC sender) through
//   an ADS1115, a K-type thermocouple under each spark plug through two
//   MAX31856, the revs from the coil, an optional wheel speed sensor, the
//   supply voltage, and the oil pressure warning switch.
//   Outputs drive relay coils: sidelights, daytime running lights, left and
//   right indicators (with hazards), brake lights, three spares.
//
// The lights work on their own: nothing here waits for the dash, so a dead
// or missing screen never takes a lamp out. Safety, whatever else happens:
//  - every output is off from power-up until the inputs have been read
//  - a watchdog restarts the box if the loop ever hangs (outputs drop off,
//    then come back as the switches say)
//  - brake lights stay wired straight to the brake switch unless
//    BRAKE_VIA_BOX is 1; the box then only reads the switch
//
// The box is on a permanent feed so the hazards and parking lights work
// with the key out. With the ignition, hazards and sidelights all off it
// sleeps (well under 1 mA) and wakes when any of them comes on.

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_now.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include <esp_wifi.h>
#include <driver/rtc_io.h>
#include <math.h>
#include "enginelink.h"

// ================================================================ settings
// What's fitted. 0 leaves a sensor out: the dash shows "--" for it.
#define OIL_PSI_FITTED 1
#define OIL_TEMP_FITTED 1
#define CHT_FITTED 2          // thermocouples: 0, 1 (left only) or 2
#define TACHO_FITTED 1        // the coil's pulses come here rather than to the dash
#define WHEEL_FITTED 0        // a hall sensor for road speed

// Brake lights: 0 = wired straight to the brake switch, as the car has them
// (the box only reads the switch, DO5 is a spare). 1 = DO5 switches them.
#define BRAKE_VIA_BOX 0

// Indicators: 90 flashes a minute, half on (the law allows 60 to 120)
const uint32_t FLASH_PERIOD_MS = 667;
// Daytime running lights: on with the ignition and the engine running,
// off while the sidelights are on. DRL_NEEDS_ENGINE 0: on with the ignition.
#define DRL_NEEDS_ENGINE 1
const float ENGINE_RUNNING_RPM = 300;
// 1: the DRLs go out while the indicators flash (needed if a DRL sits
// within 40 mm of an indicator)
#define DRL_OFF_WHEN_INDICATING 0

// Oil pressure transducer: 0.5 V at 0, 4.5 V at full scale, through a
// 10k / 20k divider (x 2/3) so the ADS1115 never sees more than 3.3 V.
const float OIL_P_DIVIDER = 1.5f;     // volts at the sensor per volt at the ADC
const float OIL_P_V0 = 0.5f, OIL_P_V1 = 4.5f;
const float OIL_P_FULL_PSI = 100;     // the transducer's range
// Oil temperature: NTC sender to ground, a 1k pull-up to 3.3 V. The table
// is a common Bosch-style 2-wire sender; put your sender's figures in here.
const float NTC_PULLUP = 1000, ADC_SUPPLY = 3.3f;
const float NTC_TABLE[][2] = {  // degrees C, ohms
    {-10, 9397}, {0, 5896}, {20, 2500}, {40, 1175}, {60, 596},
    {80, 323},   {100, 186}, {120, 113}, {130, 89},  {150, 55},
};
// Supply voltage: 47k / 10k divider (x 5.7)
const float BATT_DIVIDER = 5.7f;
const float SPARE_DIVIDER = 1;        // the spare analogue input, straight in
// Revs: one spark per turn of the crank (the 2CV's wasted spark coil)
const float TACHO_PULSES_PER_REV = 1.0f;
const float TACHO_MAX_RPM = 7000;
// Wheel sensor: pulses per mile (count the bolts or magnets it sees per
// turn, times turns per mile)
const float WHEEL_PULSES_PER_MILE = 4000;

// ================================================================ pins
// ESP32-S3-DevKitC-1. Inputs come through optocouplers (LOW = 12 V there);
// outputs drive a ULN2803 or MOSFET module (HIGH = relay on).
const int PIN_SDA = 8, PIN_SCL = 9;                     // ADS1115, PCF8574
const int PIN_SCK = 12, PIN_MOSI = 11, PIN_MISO = 13;   // MAX31856s
const int PIN_CS_CHT_L = 10, PIN_CS_CHT_R = 14;
const int PIN_TACHO = 15, PIN_WHEEL = 16;
// DI1..DI8. The wake-up inputs (ignition, hazards, sidelights) must be
// GPIO 0-21.
enum { DI_IND_L, DI_IND_R, DI_HAZARD, DI_BRAKE, DI_SIDE, DI_BEAM, DI_OIL_SW, DI_IGN };
const int DI_PINS[8] = {4, 5, 6, 7, 1, 2, 17, 18};
// DO1..DO8. GPIO 3 and 46 are read at power-up, which a ULN2803's own
// pull-downs don't upset; nothing else may pull them.
enum { DO_SIDE, DO_DRL, DO_IND_L, DO_IND_R, DO_BRAKE, DO_SPARE1, DO_SPARE2, DO_SPARE3 };
const int DO_PINS[8] = {21, 39, 40, 41, 42, 47, 3, 46};
const uint8_t ADS_ADDR = 0x48;     // ADDR to GND
const uint8_t SPARE_PCF = 0x20;    // the spare inputs' PCF8574, A0-A2 to GND
const uint32_t SLEEP_AFTER_MS = 30000;  // everything off this long: sleep

// ================================================================ state
Preferences prefs;
const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t inputs = 0;          // debounced, bit n = DI(n+1) on
uint8_t outputs = 0;
uint8_t spareOut = 0;        // DO6..DO8 asked for over serial, bits 5-7
uint8_t spareIn = 0;
bool spareInOk = false;
float oilPsi = NAN, oilTemp = NAN, chtL = NAN, chtR = NAN, rpm = 0, wheelMph = 0, battV = NAN, spareV = NAN;
bool adsOk = false;
uint32_t flashStart = 0;     // when the indicators started this run of flashes
bool flashing = false;
uint32_t quietSince = 0;     // ignition, hazards and sidelights all off since

bool in(int n) { return inputs & (1 << n); }

// ================================================================ inputs
// every 5 ms; a change has to be there for three readings in a row
void readInputs() {
  static uint8_t differ[8];
  for (int i = 0; i < 8; i++) {
    bool on = digitalRead(DI_PINS[i]) == LOW;
    if (on == in(i)) differ[i] = 0;
    else if (++differ[i] >= 3) {
      inputs ^= 1 << i;
      differ[i] = 0;
    }
  }
}

void readSpareInputs() {
  spareInOk = Wire.requestFrom(SPARE_PCF, (uint8_t)1) == 1;
  if (spareInOk) spareIn = ~Wire.read();  // optocouplers pull low: 1 = closed
}

// ================================================================ outputs
void writeOutputs(uint8_t bits) {
  for (int i = 0; i < 8; i++) digitalWrite(DO_PINS[i], (bits >> i) & 1);
  outputs = bits;
}

void updateLights() {
  uint32_t now = millis();
  bool hazard = in(DI_HAZARD);
  bool left = hazard || (in(DI_IND_L) && in(DI_IGN));
  bool right = hazard || (in(DI_IND_R) && in(DI_IGN));
  // the flash starts lit the moment the stalk moves, and keeps its rhythm
  // when the hazards take over from one side
  if (left || right) {
    if (!flashing) { flashing = true; flashStart = now; }
  } else {
    flashing = false;
  }
  bool lit = flashing && (now - flashStart) % FLASH_PERIOD_MS < FLASH_PERIOD_MS / 2;

  bool running = !DRL_NEEDS_ENGINE || rpm > ENGINE_RUNNING_RPM;
  bool drl = in(DI_IGN) && running && !in(DI_SIDE);
  if (DRL_OFF_WHEN_INDICATING && flashing) drl = false;

  uint8_t want = spareOut & 0xE0;
  if (in(DI_SIDE)) want |= 1 << DO_SIDE;
  if (drl) want |= 1 << DO_DRL;
  if (lit && left) want |= 1 << DO_IND_L;
  if (lit && right) want |= 1 << DO_IND_R;
  if (BRAKE_VIA_BOX && in(DI_BRAKE)) want |= 1 << DO_BRAKE;
  if (!BRAKE_VIA_BOX) want |= spareOut & (1 << DO_BRAKE);  // a spare then
  if (want != outputs) writeOutputs(want);
}

// ================================================================ ADS1115
// One channel at a time, single shot, +-4.096 V, 128 a second: start one,
// read it 10 ms later, start the next.
const uint8_t ADS_CONV = 0x00, ADS_CONFIG = 0x01;
int adsChan = 0;
uint32_t adsStarted = 0;
float adsVolts[4] = {NAN, NAN, NAN, NAN};

bool adsWrite(uint8_t reg, uint16_t v) {
  Wire.beginTransmission(ADS_ADDR);
  Wire.write(reg);
  Wire.write(v >> 8);
  Wire.write(v & 0xFF);
  return Wire.endTransmission() == 0;
}

bool adsStart(int ch) {
  // OS=1, MUX=1xx (AINn to GND), PGA=001 (4.096 V), single shot, 128 SPS, comparator off
  uint16_t cfg = 0x8000 | ((4 + ch) << 12) | (1 << 9) | (1 << 8) | (4 << 5) | 3;
  return adsWrite(ADS_CONFIG, cfg);
}

bool adsRead(float &v) {
  Wire.beginTransmission(ADS_ADDR);
  Wire.write(ADS_CONV);
  if (Wire.endTransmission() != 0 || Wire.requestFrom(ADS_ADDR, (uint8_t)2) != 2) return false;
  int16_t raw = (Wire.read() << 8) | Wire.read();
  v = raw * 4.096f / 32768;
  return true;
}

float interp(float x, const float t[][2], int n, int xi, int yi) {
  // the table's x column may run up or down
  bool up = t[n - 1][xi] > t[0][xi];
  for (int i = 0; i < n - 1; i++) {
    float a = t[i][xi], b = t[i + 1][xi];
    if (up ? x <= b : x >= b) return t[i][yi] + (t[i + 1][yi] - t[i][yi]) * (x - a) / (b - a);
  }
  return t[n - 1][yi];
}

void convertAnalogue() {
  const int NT = sizeof NTC_TABLE / sizeof NTC_TABLE[0];
  float v = adsVolts[0] * OIL_P_DIVIDER;  // the transducer's own output
  // under 0.25 V or over 4.75 V: a broken wire or a short, not a pressure
  oilPsi = OIL_PSI_FITTED && adsOk && v > 0.25f && v < 4.75f
               ? fmaxf(0, (v - OIL_P_V0) / (OIL_P_V1 - OIL_P_V0) * OIL_P_FULL_PSI) : NAN;
  v = adsVolts[1];
  float ohms = v > 0.02f && v < ADC_SUPPLY - 0.02f ? NTC_PULLUP * v / (ADC_SUPPLY - v) : NAN;
  oilTemp = OIL_TEMP_FITTED && adsOk && !isnan(ohms) && ohms < NTC_TABLE[0][1] * 1.5f && ohms > NTC_TABLE[NT - 1][1] * 0.5f
                ? interp(ohms, NTC_TABLE, NT, 1, 0) : NAN;
  spareV = adsOk ? adsVolts[2] * SPARE_DIVIDER : NAN;
  battV = adsOk ? adsVolts[3] * BATT_DIVIDER : NAN;
}

void pollAds() {
  if (millis() - adsStarted < 10) return;
  float v;
  if (adsOk && adsRead(v)) adsVolts[adsChan] = v;
  if (++adsChan == 4) { adsChan = 0; convertAnalogue(); }
  adsOk = adsStart(adsChan);
  adsStarted = millis();
}

// ================================================================ MAX31856
// K-type, converting all the time, 50 Hz rejection, 4 readings averaged.
// Its fault checks for an open thermocouple are left off: spark plug
// washers are grounded to the head, which upsets them.
SPISettings maxSpi(1000000, MSBFIRST, SPI_MODE1);

void maxWrite(int cs, uint8_t reg, uint8_t v) {
  SPI.beginTransaction(maxSpi);
  digitalWrite(cs, LOW);
  SPI.transfer(reg | 0x80);
  SPI.transfer(v);
  digitalWrite(cs, HIGH);
  SPI.endTransaction();
}

void maxReadN(int cs, uint8_t reg, uint8_t *b, int n) {
  SPI.beginTransaction(maxSpi);
  digitalWrite(cs, LOW);
  SPI.transfer(reg);
  for (int i = 0; i < n; i++) b[i] = SPI.transfer(0);
  digitalWrite(cs, HIGH);
  SPI.endTransaction();
}

void maxBegin(int cs) {
  pinMode(cs, OUTPUT);
  digitalWrite(cs, HIGH);
  maxWrite(cs, 0x00, 0x81);  // CR0: convert continuously, 50 Hz filter
  maxWrite(cs, 0x01, 0x23);  // CR1: 4 samples averaged, type K
}

// degrees C, or NAN if the chip isn't there or reports a fault
float maxTemp(int cs) {
  uint8_t b[4];
  maxReadN(cs, 0x0C, b, 4);  // LTCBH, LTCBM, LTCBL, SR
  if (b[3] & 0xC2) return NAN;  // out of range, or a voltage on the inputs
  uint8_t cr1;
  maxReadN(cs, 0x01, &cr1, 1);
  if (cr1 != 0x23) return NAN;  // no chip there (reads 0x00 or 0xFF)
  int32_t raw = (int32_t)((uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8) >> 13;
  float t = raw * 0.0078125f;
  return t > -40 && t < 600 ? t : NAN;
}

void pollCht() {
  static uint32_t last = 0;
  if (millis() - last < 200) return;
  last = millis();
  chtL = CHT_FITTED >= 1 ? maxTemp(PIN_CS_CHT_L) : NAN;
  chtR = CHT_FITTED >= 2 ? maxTemp(PIN_CS_CHT_R) : NAN;
}

// ================================================================ revs and wheel speed
// The optocoupler pulls the pin low at each spark. The coil rings for a
// while after that, so edges too soon after the last are ignored.
struct PulseIn {
  uint32_t minUs;
  volatile uint32_t lastUs, count, sumUs;
};
PulseIn tacho = {(uint32_t)(60e6f / (TACHO_MAX_RPM * 1.25f * TACHO_PULSES_PER_REV))};
PulseIn wheel = {1000};  // 1 ms: a thousand pulses a second is far beyond any road speed

void IRAM_ATTR pulseEdge(PulseIn &p) {
  uint32_t now = micros(), dt = now - p.lastUs;
  if (dt < p.minUs) return;
  if (p.lastUs && dt < 1000000) {
    p.sumUs += dt;
    p.count++;
  }
  p.lastUs = now;
}
void IRAM_ATTR tachoIsr() { pulseEdge(tacho); }
void IRAM_ATTR wheelIsr() { pulseEdge(wheel); }

// pulses a second over the time since last asked; 0 after timeoutMs without one
float pulseRate(PulseIn &p, float &last, uint32_t timeoutMs) {
  noInterrupts();
  uint32_t n = p.count, sum = p.sumUs, at = p.lastUs;
  p.count = p.sumUs = 0;
  interrupts();
  if (n) last = 1e6f * n / sum;
  else if (micros() - at > timeoutMs * 1000UL) last = 0;
  return last;
}

void updatePulses() {
  static uint32_t lastAt = 0;
  static float tachoHz = 0, wheelHz = 0;
  if (millis() - lastAt < 100) return;
  lastAt = millis();
  rpm = pulseRate(tacho, tachoHz, 500) * 60 / TACHO_PULSES_PER_REV;
  wheelMph = pulseRate(wheel, wheelHz, 1500) * 3600 / WHEEL_PULSES_PER_MILE;
}

// ================================================================ radio
uint8_t seq = 0;

void sendState() {
  EnginePkt p = {};
  p.magic = ENGINE_MAGIC;
  p.seq = seq++;
  p.spareIn = spareIn;
  // tenths, and the bit that says the reading is good
  auto put = [&](float v, uint16_t bit) -> int16_t {
    if (isnan(v)) return 0;
    p.valid |= bit;
    return (int16_t)lroundf(constrain(v * 10, -32000, 32000));
  };
  p.oilPsi10 = put(oilPsi, EV_OIL_PSI);
  p.oilTemp10 = put(oilTemp, EV_OIL_TEMP);
  p.chtL10 = put(chtL, EV_CHT_L);
  p.chtR10 = put(chtR, EV_CHT_R);
  if (TACHO_FITTED) { p.rpm = (uint16_t)lroundf(rpm); p.valid |= EV_RPM; }
  if (WHEEL_FITTED) { p.wheelMph10 = (uint16_t)lroundf(wheelMph * 10); p.valid |= EV_WHEEL; }
  if (!isnan(battV)) { p.battMv = (uint16_t)lroundf(battV * 1000); p.valid |= EV_BATT; }
  if (!isnan(spareV)) p.spareMv = (uint16_t)lroundf(fmaxf(0, spareV) * 1000);

  uint16_t l = 0;
  if (outputs & (1 << DO_IND_L)) l |= EL_IND_L;
  if (outputs & (1 << DO_IND_R)) l |= EL_IND_R;
  if (in(DI_HAZARD)) l |= EL_HAZARD;
  if (outputs & (1 << DO_SIDE)) l |= EL_SIDE;
  if (in(DI_BEAM)) l |= EL_BEAM;
  if (in(DI_BRAKE)) l |= EL_BRAKE;
  if (outputs & (1 << DO_DRL)) l |= EL_DRL;
  if (in(DI_IGN)) l |= EL_IGN;
  if (in(DI_OIL_SW)) l |= EL_OIL_LOW;
  p.lights = l;
  p.outputs = outputs;
  p.inputs = inputs;
  p.uptime = millis() / 1000;
  esp_now_send(BROADCAST, (const uint8_t *)&p, sizeof p);
}

void radioBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ENGINE_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) Serial.println("[box] ESP-NOW failed to start");
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, BROADCAST, 6);
  p.channel = 0;
  p.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&p);
  Serial.printf("[box] engine box %s on channel %d\n", WiFi.macAddress().c_str(), ENGINE_CHANNEL);
}

// ================================================================ sleep
// Asleep, every output pin floats and the relay drivers' pull-downs keep
// them off. The ignition, hazard or sidelight input coming on wakes it.
void sleepIfQuiet() {
  uint32_t now = millis();
  bool busy = in(DI_IGN) || in(DI_HAZARD) || in(DI_SIDE) || (BRAKE_VIA_BOX && in(DI_BRAKE)) || (spareOut & 0xF0);
  if (busy || !quietSince) { quietSince = busy ? 0 : now; return; }
  if (now - quietSince < SLEEP_AFTER_MS) return;
  Serial.println("[box] all off: sleeping");
  Serial.flush();
  writeOutputs(0);
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);
  uint64_t mask = (1ULL << DI_PINS[DI_IGN]) | (1ULL << DI_PINS[DI_HAZARD]) | (1ULL << DI_PINS[DI_SIDE]);
  if (BRAKE_VIA_BOX) mask |= 1ULL << DI_PINS[DI_BRAKE];
  // the pins' own pull-ups don't last into sleep: the RTC's do
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  for (int i = 0; i < 64; i++)
    if (mask & (1ULL << i)) {
      rtc_gpio_pullup_en((gpio_num_t)i);
      rtc_gpio_pulldown_dis((gpio_num_t)i);
    }
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  esp_deep_sleep_start();
}

// ================================================================ serial
// "out 6 on" / "out 6 off" switch a spare output (6-8, or 5 when the brake
// lights are wired straight); "status" prints everything.
void printStatus() {
  Serial.printf("[box] oil %.1f psi %.1f C  heads %.1f / %.1f C  %.0f rpm  %.1f mph  supply %.2f V  spare %.2f V\n",
                oilPsi, oilTemp, chtL, chtR, rpm, wheelMph, battV, spareV);
  Serial.printf("[box] inputs %02X  outputs %02X  spare inputs %02X%s  ADC %s\n", inputs, outputs, spareIn,
                spareInOk ? "" : " (no PCF8574)", adsOk ? "OK" : "NOT FOUND");
}

void handleSerial() {
  static char line[24];
  static int n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') {
      if (n < (int)sizeof line - 1) line[n++] = c;
      continue;
    }
    line[n] = 0;
    n = 0;
    int ch;
    char state[4];
    if (!strcmp(line, "status")) printStatus();
    else if (sscanf(line, "out %d %3s", &ch, state) == 2 && (ch >= 6 || (ch == 5 && !BRAKE_VIA_BOX)) && ch <= 8) {
      uint8_t bit = 1 << (ch - 1);
      spareOut = !strcmp(state, "on") ? spareOut | bit : spareOut & ~bit;
      Serial.printf("[box] DO%d %s\n", ch, spareOut & bit ? "on" : "off");
    }
  }
}

// ================================================================ start-up
void setup() {
  // outputs off before anything else
  for (int p : DO_PINS) {
    digitalWrite(p, LOW);
    pinMode(p, OUTPUT);
  }
  for (int p : DI_PINS) pinMode(p, INPUT_PULLUP);
  Serial.begin(115200);
  esp_task_wdt_config_t wdt = {2000, 0, true};  // 2 s, then restart
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
  esp_task_wdt_add(nullptr);

  // the switches as they are now, before any lamp is lit
  for (int i = 0; i < 4; i++) { readInputs(); delay(5); }

  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  adsOk = adsStart(0);
  adsStarted = millis();
  Serial.printf("[box] ADS1115 %s\n", adsOk ? "OK" : "NOT FOUND");
  readSpareInputs();
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI);
  maxBegin(PIN_CS_CHT_L);
  maxBegin(PIN_CS_CHT_R);
  pinMode(PIN_TACHO, INPUT_PULLUP);
  if (TACHO_FITTED) attachInterrupt(digitalPinToInterrupt(PIN_TACHO), tachoIsr, FALLING);
  pinMode(PIN_WHEEL, INPUT_PULLUP);
  if (WHEEL_FITTED) attachInterrupt(digitalPinToInterrupt(PIN_WHEEL), wheelIsr, FALLING);
  radioBegin();
}

void loop() {
  static uint32_t lastInputs = 0, lastSend = 0, lastSpare = 0;
  static uint16_t sentLights = 0;
  uint32_t now = millis();
  esp_task_wdt_reset();
  if (now - lastInputs >= 5) { lastInputs = now; readInputs(); }
  updatePulses();
  updateLights();
  pollAds();
  pollCht();
  if (now - lastSpare >= 50) { lastSpare = now; readSpareInputs(); }
  // straight away when a lamp changes, so the dash's arrows flash in step
  uint16_t lightsNow = outputs | (inputs << 8);
  if (now - lastSend >= ENGINE_EVERY_MS || lightsNow != sentLights) {
    sendState();
    lastSend = now;
    sentLights = lightsNow;
  }
  handleSerial();
  sleepIfQuiet();
  delay(1);
}
