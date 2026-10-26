// Wireless relay box for the Autoterm screen
// Target: Waveshare ESP32-S3-POE-ETH-8DI-8DO, built with `pio run -e relaybox`.
//
// The screen sends what it wants over ESP-NOW (relaylink.h) and this box
// switches its outputs to match, then reports back. The outputs are
// optocoupled Darlington transistors that sink up to 500 mA: they drive the
// coils of automotive relays (or a windlass contactor), never a load itself.
//
//   DO1 lights        DO2 water pump    DO3 bilge pump (manual run)
//   DO4 windlass UP   DO5 windlass DOWN DO6..DO8 spare
//   DI1..DI8 read and reported to the screen, not used yet
//
// Safety, enforced here whatever the screen sends:
//  - powers up with every output off
//  - windlass is hold-to-run: it stops if no keep-alive arrives for 300 ms
//  - UP and DOWN are never on together, with a pause between directions
//  - after 10 s without the screen everything but the bilge pump goes off
//
// Pairing: a box with no screen broadcasts that it is pairing and takes the
// first screen that joins. Hold BOOT for 3 seconds to forget the screen and
// pair again. The RGB LED: blue blinking = pairing, green = screen heard,
// red = screen lost, red blinking = output chip not answering.

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "relaylink.h"

// ---------------------------------------------------------------- board
// The outputs are on a TCA9554 I/O expander; the RTC shares its I2C bus.
const int PIN_SDA = 42, PIN_SCL = 41;
const uint8_t EXP_ADDR = 0x20;
const uint8_t EXP_OUTPUT = 0x01, EXP_CONFIG = 0x03;
// An output is on while its expander pin is LOW (it lights the optocoupler).
// The expander starts with every pin an input, i.e. every output off.
// If DO1 lights when LIGHTS is off on the screen, flip this.
const bool OUT_ACTIVE_LOW = true;
const int DI_PINS[8] = {4, 5, 6, 7, 8, 9, 10, 11};  // optocoupled, LOW = closed
const int PIN_LED = 38;   // one WS2812
const int PIN_BOOT = 0;

const uint8_t RADIO_CHANNEL = 1;  // as the screens' link
const uint32_t WINCH_DEADTIME_MS = 250;  // all stopped before changing direction
const uint32_t STATE_EVERY_MS = 500;     // while pairing, and as a heartbeat

// ---------------------------------------------------------------- state
Preferences prefs;
uint8_t screenMac[6];
bool screenKnown = false;
const uint8_t BROADCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

uint8_t want = 0;               // outputs the screen asked for, windlass bits cleared
BoxWinch winchWant = BOXW_STOP;
uint32_t lastCmdAt = 0;         // 0: never heard the screen since power-up
uint8_t outputs = 0;            // what is on now
uint32_t winchStoppedAt = 0;
uint8_t inputs = 0;
bool expOk = false;

// ---------------------------------------------------------------- outputs
bool expWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(EXP_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool writeOutputs(uint8_t bits) {
  return expWrite(EXP_OUTPUT, OUT_ACTIVE_LOW ? ~bits : bits);
}

// set the "off" level first, then make the pins outputs, so nothing blips on
void outputsBegin() {
  Wire.begin(PIN_SDA, PIN_SCL, 100000);
  expOk = writeOutputs(0) && expWrite(EXP_CONFIG, 0x00);
  Serial.printf("[box] output chip %s\n", expOk ? "OK" : "NOT FOUND");
}

const char *const OUT_NAME[8] = {"lights", "water pump", "bilge pump", "windlass UP", "windlass DOWN", "DO6", "DO7", "DO8"};

void setOutputs(uint8_t bits) {
  // never both windlass directions, whatever went wrong above
  if ((bits & WINCH_BITS) == WINCH_BITS) bits &= ~WINCH_BITS;
  if (bits == outputs && expOk) return;
  bool ok = writeOutputs(bits);
  if (!ok) {
    // try once to bring the chip back, e.g. after a loose connection
    outputsBegin();
    ok = expOk && writeOutputs(bits);
  }
  expOk = ok;
  if (!ok) return;
  for (int i = 0; i < 8; i++)
    if (((bits ^ outputs) >> i) & 1) Serial.printf("[box] %s %s\n", OUT_NAME[i], (bits >> i) & 1 ? "ON" : "off");
  outputs = bits;
}

// work out what should be on now and switch to it
void applyOutputs() {
  uint32_t now = millis();
  bool heard = lastCmdAt && now - lastCmdAt < BOX_LOST_MS;
  uint8_t bits;
  if (heard) bits = want & ~WINCH_BITS;
  else bits = outputs & (1 << OUT_BILGE_PUMP);  // lost: leave only a bilge run going

  // windlass: hold-to-run, one direction at a time, a pause between them
  BoxWinch w = (heard && now - lastCmdAt < BOX_WINCH_HOLD_MS) ? winchWant : BOXW_STOP;
  uint8_t dir = w == BOXW_UP ? 1 << OUT_WINCH_UP : w == BOXW_DOWN ? 1 << OUT_WINCH_DOWN : 0;
  uint8_t running = outputs & WINCH_BITS;
  if (running && running != dir) {
    dir = 0;  // stop first
    winchStoppedAt = now;
  } else if (!running && dir && now - winchStoppedAt < WINCH_DEADTIME_MS) {
    dir = 0;  // still in the pause after the last run
  }
  if (running && !dir) winchStoppedAt = now;
  setOutputs(bits | dir);
}

void readInputs() {
  uint8_t in = 0;
  for (int i = 0; i < 8; i++) in |= (digitalRead(DI_PINS[i]) == LOW) << i;
  if (in != inputs) Serial.printf("[box] inputs %02X\n", in);
  inputs = in;
}

// ---------------------------------------------------------------- radio
struct RxMsg {
  uint8_t mac[6];
  BoxCmdPkt pkt;
};
RxMsg rxQueue[8];
volatile uint8_t rxHead = 0, rxTail = 0;
portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;

void queueRadio(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != sizeof(BoxCmdPkt)) return;
  portENTER_CRITICAL(&rxMux);
  uint8_t next = (rxHead + 1) % 8;
  if (next != rxTail) {
    memcpy(rxQueue[rxHead].mac, mac, 6);
    memcpy(&rxQueue[rxHead].pkt, data, len);
    rxHead = next;
  }
  portEXIT_CRITICAL(&rxMux);
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onRadioRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) { queueRadio(info->src_addr, data, len); }
#else
void onRadioRecv(const uint8_t *mac, const uint8_t *data, int len) { queueRadio(mac, data, len); }
#endif

void addPeer(const uint8_t *mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6);
  p.channel = 0;
  p.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&p);
}

void sendState() {
  BoxStatePkt p = {};
  p.magic = BOX_MAGIC;
  p.type = BOX_STATE;
  p.pairing = !screenKnown;
  p.outputs = outputs;
  p.inputs = inputs;
  p.ok = expOk;
  p.uptime = millis() / 1000;
  if (screenKnown) memcpy(p.screen, screenMac, 6);
  esp_now_send(BROADCAST, (const uint8_t *)&p, sizeof p);
}

void printMac(const char *what, const uint8_t *m) {
  Serial.printf("[box] %s %02X:%02X:%02X:%02X:%02X:%02X\n", what, m[0], m[1], m[2], m[3], m[4], m[5]);
}

void handleCmd(const uint8_t *mac, const BoxCmdPkt &c) {
  if (c.magic != BOX_MAGIC || c.type != BOX_CMD) return;
  if (!screenKnown) {
    if (!c.join) return;
    memcpy(screenMac, mac, 6);
    screenKnown = true;
    addPeer(screenMac);
    prefs.putBytes("screen", screenMac, 6);
    printMac("paired with screen", screenMac);
  }
  if (memcmp(mac, screenMac, 6)) return;  // only our own screen
  lastCmdAt = millis();
  want = c.outputs & ~WINCH_BITS;
  winchWant = c.winch <= BOXW_DOWN ? (BoxWinch)c.winch : BOXW_STOP;
  applyOutputs();
  sendState();  // the acknowledgement
}

void processRadio() {
  while (rxTail != rxHead) {
    RxMsg m;
    portENTER_CRITICAL(&rxMux);
    m = rxQueue[rxTail];
    rxTail = (rxTail + 1) % 8;
    portEXIT_CRITICAL(&rxMux);
    handleCmd(m.mac, m.pkt);
  }
}

void forgetScreen() {
  screenKnown = false;
  lastCmdAt = 0;
  prefs.remove("screen");
  Serial.println("[box] screen forgotten, pairing");
}

// ---------------------------------------------------------------- LED
void led(uint8_t r, uint8_t g, uint8_t b) {
  static uint32_t last = 0xFFFFFFFF;
  uint32_t c = r << 16 | g << 8 | b;
  if (c == last) return;
  last = c;
  neopixelWrite(PIN_LED, r, g, b);
}

void showStatus() {
  bool blink = (millis() / 400) & 1;
  if (!expOk) led(blink ? 40 : 0, 0, 0);
  else if (!screenKnown) led(0, 0, blink ? 40 : 0);
  else if (lastCmdAt && millis() - lastCmdAt < 2000) led(0, 30, 0);
  else led(40, 0, 0);
}

// ---------------------------------------------------------------- main
void setup() {
  Serial.begin(115200);
  outputsBegin();  // first thing: everything off
  for (int p : DI_PINS) pinMode(p, INPUT_PULLUP);
  pinMode(PIN_BOOT, INPUT_PULLUP);

  prefs.begin("box", false);
  screenKnown = prefs.isKey("screen") && prefs.getBytes("screen", screenMac, 6) == 6;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(RADIO_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) Serial.println("[box] ESP-NOW failed to start");
  esp_now_register_recv_cb(onRadioRecv);
  addPeer(BROADCAST);
  if (screenKnown) addPeer(screenMac);
  Serial.printf("[box] relay box %s on channel %d\n", WiFi.macAddress().c_str(), RADIO_CHANNEL);
  if (screenKnown) printMac("screen", screenMac);
  else Serial.println("[box] no screen yet, pairing");
}

void loop() {
  static uint32_t lastState = 0, bootDownAt = 0;
  uint32_t now = millis();

  processRadio();
  applyOutputs();  // also times out the windlass and a lost screen
  readInputs();

  if (now - lastState >= STATE_EVERY_MS) {
    sendState();
    lastState = now;
  }

  // hold BOOT 3 s to pair with another screen
  if (digitalRead(PIN_BOOT) == LOW) {
    if (!bootDownAt) bootDownAt = now;
    else if (now - bootDownAt > 3000 && screenKnown) forgetScreen();
  } else {
    bootDownAt = 0;
  }

  showStatus();
  delay(5);
}
