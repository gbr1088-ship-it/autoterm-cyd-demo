// Lomax dashboard for the ESP32-2432S028 ("Cheap Yellow Display"), the
// round Waveshare ESP32-S3-Touch-LCD-1.28-B (BOARD_ROUND), or the 7"
// Waveshare ESP32-S3-Touch-LCD-7 in landscape (BOARD_7INCH).
//
// Speed from a GPS module, engine speed from the ignition coil's pulses, the
// indicator, main beam and sidelight lamps, an odometer and trip, and an old
// style clock set from the GPS. Tap the screen for the next page; hold it to
// zero the trip. Pins and calibration are in lomax_config.h, wiring in LOMAX.md.
//
//   pio run -e lomax -t upload           (CYD)
//   pio run -e lomax_round -t upload     (round)
//   pio run -e lomax7 -t upload          (7")
#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#if defined(BOARD_ROUND) || defined(BOARD_7INCH)
#include <Wire.h>
#else
#include <XPT2046_Touchscreen.h>
#endif
#include <sys/time.h>
#include <time.h>
#include "lomax_config.h"

#if defined(BOARD_7INCH)
#include "display7.h"  // tft: a full-screen sprite, copied to the panel in the background

// the sprites keep their pixels byte-swapped, as tft does, so rows copy straight in
void screenPush(TFT_eSprite &s, int sx, int sy, int w, int h, int x, int y) {
  uint16_t *p = (uint16_t *)s.getPointer();
  for (int r = 0; r < h; r++) tft.pushImage(x, y + r, w, 1, p + (sy + r) * s.width() + sx);
}
#else
TFT_eSPI tft;

void screenPush(TFT_eSprite &s, int sx, int sy, int w, int h, int x, int y) { s.pushSprite(x, y, sx, sy, w, h); }
#endif
void screenFill(int x, int y, int w, int h, uint16_t colour) { tft.fillRect(x, y, w, h, colour); }

#include "lomax_draw.h"

#if defined(BOARD_7INCH)
// the GT911 touch is display7.h's
#elif defined(BOARD_ROUND)
// CST816S touch controller on the board's I2C bus (shared with the IMU)
#define TOUCH_SDA 6
#define TOUCH_SCL 7
#define TOUCH_RST 13
#define CST816_ADDR 0x15
#else
// touch controller on its own pins (VSPI), as on every CYD
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
SPIClass touchSpi(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
#endif

#define BL_CHANNEL 0
HardwareSerial gpsSerial(2);
Preferences prefs;
DashState st;

// ================================================================ backlight
float blLevel = 0;  // fades towards the day or night level

#if defined(BOARD_7INCH)
// The 7"'s backlight is on or off, so it dims by darkening the picture on
// its way to the panel instead.
float shownLevel = -1;

void backlight(uint8_t v) {
  static int on = -1;
  if ((v > 0) != on) display7Backlight(on = v > 0);  // an I2C write: only on a change
  float level = v / 255.0f;
  if (v && fabsf(level - shownLevel) > 0.02f) {
    makeGammaLuts(level);
    tft.mark(0, 0, UI_W, UI_H);  // send the whole screen again, darker or lighter
    shownLevel = level;
  }
}
#else
void backlight(uint8_t v) { ledcWrite(BL_CHANNEL, v); }
#endif

void fadeBacklight(float dt) {
  float want = st.side ? BRIGHT_NIGHT : BRIGHT_DAY;
  blLevel += (want - blLevel) * fminf(1, dt * 3);
  backlight((uint8_t)roundf(blLevel));
}

// ================================================================ tacho
// The optocoupler pulls the pin low each time the points open (the spark).
// The coil rings for a while after that, so edges too soon after the last
// one are ignored.
const uint32_t TACHO_MIN_US = (uint32_t)(60e6f / (TACHO_MAX_RPM * 1.25f * TACHO_PULSES_PER_REV));
volatile uint32_t tachoLastUs = 0, tachoCount = 0, tachoSumUs = 0;

void IRAM_ATTR tachoIsr() {
  uint32_t now = micros(), dt = now - tachoLastUs;
  if (dt < TACHO_MIN_US) return;
  if (tachoLastUs && dt < 1000000) {
    tachoSumUs += dt;
    tachoCount++;
  }
  tachoLastUs = now;
}

// every 100 ms: the average over the pulses since last time
void updateRpm() {
  noInterrupts();
  uint32_t n = tachoCount, sum = tachoSumUs, last = tachoLastUs;
  tachoCount = tachoSumUs = 0;
  interrupts();
  if (n) st.rpm = 60e6f * n / (sum * TACHO_PULSES_PER_REV);
  else if (micros() - last > TACHO_TIMEOUT_MS * 1000UL) st.rpm = 0;
}

// ================================================================ lamps
struct Lamp {
  int pin;
  bool *state;
  uint8_t differ;  // readings in a row that differ from the state
};
Lamp lamps[] = {{IND_L_PIN, &st.indL, 0}, {IND_R_PIN, &st.indR, 0}, {BEAM_PIN, &st.beam, 0}, {SIDE_PIN, &st.side, 0}};

#if defined(LAMP_PCF8574)
// The lamps come through a PCF8574 on the I2C bus: the "pins" are its
// inputs P0-P7. Writing 1s makes them inputs with weak pull-ups.
uint8_t expanderBits = 0xFF;

void beginLamps() {
  Wire.beginTransmission(LAMP_PCF8574);
  Wire.write(0xFF);
  if (Wire.endTransmission() != 0) Serial.println("[lamps] no PCF8574 answering");
}

bool lampLevel(int pin) { return (expanderBits >> pin) & 1; }
#else
void beginLamps() {
  for (auto &l : lamps)
    if (l.pin >= 0) pinMode(l.pin, LAMPS_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
}

bool lampLevel(int pin) { return digitalRead(pin); }
#endif

// every 10 ms; a change has to be there for two readings
void readLamps() {
#if defined(LAMP_PCF8574)
  if (Wire.requestFrom((uint8_t)LAMP_PCF8574, (uint8_t)1) == 1) expanderBits = Wire.read();
#endif
  for (auto &l : lamps) {
    if (l.pin < 0) continue;
    bool lit = lampLevel(l.pin) == (LAMPS_ACTIVE_LOW ? LOW : HIGH);
    if (lit == *l.state) l.differ = 0;
    else if (++l.differ >= 2) { *l.state = lit; l.differ = 0; }
  }
  if (IND_R_PIN < 0) st.indR = st.indL;  // one indicator lamp: both arrows
}

// ================================================================ GPS
// A small NMEA reader: $--RMC gives the fix, speed, date and time.
char nmea[100];
int nmeaLen = 0;
uint32_t lastFixMs = 0, lastSpeedMs = 0;
bool clockSet = false;
float savedOdo = 0;

int hex(char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; }

bool nmeaChecksumOk(const char *s) {
  const char *star = strchr(s, '*');
  if (!star || !star[1] || !star[2]) return false;
  uint8_t x = 0;
  for (const char *p = s + 1; p < star; p++) x ^= *p;
  return x == hex(star[1]) * 16 + hex(star[2]);
}

// the n-th comma separated field (0 = the sentence name), copied into out
void field(const char *s, int n, char *out, int len) {
  while (n-- && (s = strchr(s, ','))) s++;
  int i = 0;
  if (s)
    while (*s && *s != ',' && *s != '*' && i < len - 1) out[i++] = *s++;
  out[i] = 0;
}

// days since 1970-01-01 for a civil date (Howard Hinnant's algorithm)
long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}

void saveDistance() {
  prefs.putFloat("odo", st.odo);
  prefs.putFloat("trip", st.trip);
  savedOdo = st.odo;
}

void handleRmc(const char *s) {
  char f[16];
  field(s, 2, f, sizeof f);
  if (f[0] != 'A') return;  // V: no fix
  uint32_t now = millis();
  lastFixMs = now;

  field(s, 7, f, sizeof f);
  float mph = atof(f) * 1.15078f;  // knots
  if (mph < GPS_MIN_MPH) mph = 0;
  // distance since the last fix; a gap of more than 2 s isn't counted
  if (lastSpeedMs && now - lastSpeedMs < 2000 && mph > 0) {
    float miles = mph * (now - lastSpeedMs) / 3.6e6f;
    st.odo += miles;
    st.trip += miles;
  }
  lastSpeedMs = now;
  st.mph = mph;
  // save every tenth of a mile, and on stopping (the ignition may go off next)
  if (st.odo - savedOdo >= 0.1f || (mph == 0 && st.odo != savedOdo)) saveDistance();

  // time and date: hhmmss.ss and ddmmyy
  char t[16], d[8];
  field(s, 1, t, sizeof t);
  field(s, 9, d, sizeof d);
  if (strlen(t) >= 6 && strlen(d) == 6) {
    int hh = (t[0] - '0') * 10 + t[1] - '0', mi = (t[2] - '0') * 10 + t[3] - '0', ss = (t[4] - '0') * 10 + t[5] - '0';
    int dd = (d[0] - '0') * 10 + d[1] - '0', mo = (d[2] - '0') * 10 + d[3] - '0', yy = 2000 + (d[4] - '0') * 10 + d[5] - '0';
    time_t utc = (time_t)daysFromCivil(yy, mo, dd) * 86400 + hh * 3600 + mi * 60 + ss;
    if (yy >= 2024 && (!clockSet || labs((long)(utc - time(nullptr))) > 1)) {
      timeval tv = {utc, 0};
      settimeofday(&tv, nullptr);
      clockSet = true;
    }
  }
}

void readGps() {
  while (gpsSerial.available()) {
    char c = gpsSerial.read();
    if (c == '$') nmeaLen = 0;
    if (c == '\r' || c == '\n') {
      if (nmeaLen > 6) {
        nmea[nmeaLen] = 0;
        if (!strncmp(nmea + 3, "RMC", 3) && nmeaChecksumOk(nmea)) handleRmc(nmea);
      }
      nmeaLen = 0;
    } else if (nmeaLen < (int)sizeof nmea - 1) {
      nmea[nmeaLen++] = c;
    }
  }
  st.gpsOk = lastFixMs && millis() - lastFixMs < GPS_TIMEOUT_MS;
  if (!st.gpsOk) st.mph = 0;
}

#if GPS_TX_PIN >= 0
// u-blox: RMC only, 5 times a second (the GPS keeps this until it loses power)
void ubx(uint8_t cls, uint8_t id, const uint8_t *p, uint16_t n) {
  uint8_t a = 0, b = 0, head[4] = {cls, id, (uint8_t)n, (uint8_t)(n >> 8)};
  gpsSerial.write(0xB5);
  gpsSerial.write(0x62);
  for (uint8_t v : head) { gpsSerial.write(v); a += v; b += a; }
  for (int i = 0; i < n; i++) { gpsSerial.write(p[i]); a += p[i]; b += a; }
  gpsSerial.write(a);
  gpsSerial.write(b);
}

void setupGps() {
  for (uint8_t msg : {0x00, 0x01, 0x02, 0x03, 0x05}) {  // GGA, GLL, GSA, GSV, VTG off
    uint8_t p[3] = {0xF0, msg, 0};
    ubx(0x06, 0x01, p, 3);
    delay(20);
  }
  uint8_t rate[6] = {200, 0, 1, 0, 1, 0};  // 200 ms, every cycle, GPS time
  ubx(0x06, 0x08, rate, 6);
}
#endif

// ================================================================ clock
void updateClock() {
  st.timeOk = clockSet;
  if (!clockSet) {  // a stopped clock, hands at ten past ten
    st.hh = 10;
    st.mm = 8;
    st.ss = 0;
    return;
  }
  time_t now = time(nullptr);
  tm lt;
  localtime_r(&now, &lt);
  st.hh = lt.tm_hour;
  st.mm = lt.tm_min;
  st.ss = lt.tm_sec;
}

// ================================================================ touch
#if defined(BOARD_7INCH)
void beginTouch() {}  // display7Begin() starts it
bool touched(int &x, int &y) { return touch7Read(x, y); }
#elif defined(BOARD_ROUND)
void beginTouch() {
  pinMode(TOUCH_RST, OUTPUT);
  digitalWrite(TOUCH_RST, LOW);
  delay(10);
  digitalWrite(TOUCH_RST, HIGH);
  delay(50);
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);
  Wire.beginTransmission(CST816_ADDR);  // keep it awake: asleep, it doesn't answer
  Wire.write(0xFE);
  Wire.write(0x01);
  Wire.endTransmission();
}

// Only "is a finger down" matters here; the position is read but unused.
bool touched(int &x, int &y) {
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(0x02);  // number of fingers, then X and Y
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(CST816_ADDR, 5) != 5) return false;
  uint8_t b[5];
  for (auto &v : b) v = Wire.read();
  if (!(b[0] & 0x0F)) return false;
  x = ((b[1] & 0x0F) << 8) | b[2];
  y = ((b[3] & 0x0F) << 8) | b[4];
  return true;
}
#else
void beginTouch() {
  touchSpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSpi);
  ts.setRotation(SCREEN_ROTATION);
}

bool touched(int &x, int &y) {
  if (!ts.tirqTouched() || !ts.touched()) return false;
  TS_Point p = ts.getPoint();
  if (p.z < 300) return false;
  x = constrain(map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, 320), 0, 319);
  y = constrain(map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 240), 0, 239);
  return true;
}
#endif

uint32_t pressStart = 0, lastTouchMs = 0;
bool pressHandled = false;

void handleTouch() {
  int x, y;
  uint32_t now = millis();
  if (touched(x, y)) lastTouchMs = now;
  bool down = now - lastTouchMs < 80;  // the panel drops out briefly mid-press
  if (down && !pressStart) { pressStart = now; pressHandled = false; }
  if (down && !pressHandled && now - pressStart >= LONG_PRESS_MS) {
    st.trip = 0;
    saveDistance();
    pressHandled = true;
    Serial.println("trip reset");
  }
  if (!down && pressStart) {
    if (!pressHandled && nextPage(page) != page) {  // the 7" has only one page
      Page next = nextPage(page);
      showPage(next, st);
      updateDash(st, 0, true);
      prefs.putUChar("page", next);
    }
    pressStart = 0;
  }
}

// ================================================================ serial
// "odo 12873.4" sets the odometer to the car's own; "trip 0" zeroes the trip.
void handleSerial() {
  static char line[32];
  static int n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c != '\n' && c != '\r') {
      if (n < (int)sizeof line - 1) line[n++] = c;
      continue;
    }
    line[n] = 0;
    n = 0;
    if (!strncmp(line, "odo ", 4)) st.odo = atof(line + 4);
    else if (!strncmp(line, "trip ", 5)) st.trip = atof(line + 5);
    else continue;
    saveDistance();
    Serial.printf("odo %.1f  trip %.1f\n", st.odo, st.trip);
  }
}

// ================================================================ start-up
// All the lamps on and the needles swept to full scale and back, like a
// car's own bulb check.
void lampCheck() {
  DashState t = st;
  t.indL = t.indR = t.beam = t.side = true;
  t.gpsOk = false;
  const uint32_t T = 1400;
  uint32_t start = millis();
  for (uint32_t now = start; now - start < T; now = millis()) {
    float k = sinf((float)(now - start) / T * 3.1416f);
    t.mph = SPEEDO_MAX * k;
    t.rpm = TACHO_MAX * 1000 * k;
    updateDash(t, 0, true);
    delay(5);
  }
}

void setup() {
  Serial.begin(115200);
  prefs.begin("lomax", false);
  st.odo = savedOdo = prefs.getFloat("odo", 0);
  st.trip = prefs.getFloat("trip", 0);

#if defined(BOARD_7INCH)
  display7Begin();  // screen, touch and the I2C bus (also the lamps' expander)
#else
  tft.init();  // switches the backlight on, so the dimmer takes the pin after it
  tft.setRotation(SCREEN_ROTATION);
  tft.fillScreen(C_DASH);
  ledcSetup(BL_CHANNEL, 5000, 8);
  ledcAttachPin(TFT_BL, BL_CHANNEL);
  backlight(0);
  beginTouch();
#endif

  // the logo fades in
  drawSplash();
  for (int i = 0; i <= 255; i += 5) { backlight(i); delay(8); }
  blLevel = BRIGHT_DAY;

  setenv("TZ", LOCAL_TZ, 1);
  tzset();
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
#if GPS_TX_PIN >= 0
  setupGps();
#endif
  pinMode(TACHO_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TACHO_PIN), tachoIsr, FALLING);
  beginLamps();

  uint32_t t0 = millis();
  while (millis() - t0 < SPLASH_MS) readGps();

  uint8_t p = prefs.getUChar("page", 255);
  showPage(hasPage(p) ? (Page)p : nextPage((Page)(PAGE_COUNT - 1)), st);
  Serial.printf("heap free %u, largest block %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
#if SHOW_LAMP_CHECK
  lampCheck();
#endif
  updateClock();
  updateDash(st, 0, true);
}

void loop() {
  static uint32_t lastLamps = 0, lastRpm = 0, lastFrame = 0;
  uint32_t now = millis();
  readGps();
  handleSerial();
  if (now - lastLamps >= 10) { lastLamps = now; readLamps(); }
  if (now - lastRpm >= 100) { lastRpm = now; updateRpm(); }
  handleTouch();
  if (now - lastFrame >= 20) {  // up to 50 frames a second, only what moved is redrawn
    float dt = (now - lastFrame) / 1000.0f;
    lastFrame = now;
    updateClock();
    updateDash(st, dt);
    fadeBacklight(dt);
  }
}
