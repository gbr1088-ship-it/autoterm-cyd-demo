// Lomax dashboard: everything that draws. No pins, timers or radios in here,
// so tools/lomax_preview can draw the same screens on a PC.
//
// Each dial's face (bezel, scale, numerals, odometer) is drawn once into a
// sprite when its page opens. Every frame the dial is sent to the screen in
// strips of STRIP_H rows: the face's rows are copied into a small strip
// sprite, the needle or hands are drawn on top, and the strip is pushed. A
// whole 200 px dial would need a second 80 KB sprite to do that in one go,
// which the ESP32 doesn't have to spare.
//
// The including file supplies, before including this:
//   TFT_eSPI tft;
//   void screenPush(TFT_eSprite &s, int sx, int sy, int w, int h, int x, int y);  // w x h of s from (sx, sy) to (x, y)
//   void screenFill(int x, int y, int w, int h, uint16_t colour);
#pragma once
#include <math.h>
#include "lomax_config.h"
#include "lomax_fonts.h"
// the winged badge for the start-up screen, made by tools/lomax_logo.py and
// tools/png2rgb565.py (see LOMAX.md); without it, a plain LOMAX badge is drawn
#if defined(BOARD_ROUND) && __has_include("lomax_logo_round.h")
#include "lomax_logo_round.h"  // narrower, to fit inside the round screen
#define HAVE_LOMAX_LOGO 1
#elif defined(BOARD_7INCH) && __has_include("lomax_logo7.h")
#include "lomax_logo7.h"       // bigger, for the 7" screen
#define HAVE_LOMAX_LOGO 1
#elif !defined(BOARD_ROUND) && !defined(BOARD_7INCH) && __has_include("lomax_logo.h")
#include "lomax_logo.h"
#define HAVE_LOMAX_LOGO 1
#endif

// the screen: the CYD's 320 x 240, the round board's 240 x 240 or the 7"'s 800 x 480
#if defined(BOARD_ROUND)
const int SCR_W = 240, SCR_H = 240;
#elif defined(BOARD_7INCH)
const int SCR_W = 800, SCR_H = 480;  // the 7" panel, landscape
#else
const int SCR_W = 320, SCR_H = 240;
#endif

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

const uint16_t C_DASH      = rgb(12, 11, 10);     // the panel around the dials
const uint16_t C_FACE      = rgb(242, 234, 212);  // aged cream
const uint16_t C_FACE_EDGE = rgb(214, 200, 168);  // the face darkens towards the bezel
const uint16_t C_INK       = rgb(30, 26, 22);     // scale and numerals
const uint16_t C_RED       = rgb(176, 32, 24);    // red band, km/h
const uint16_t C_NEEDLE    = rgb(196, 52, 22);
const uint16_t C_BLUED     = rgb(28, 42, 92);     // blued-steel clock hands
const uint16_t C_BRASS     = rgb(184, 150, 80);
const uint16_t C_LAMP_OFF  = rgb(40, 39, 36);     // an unlit lamp, just visible
const uint16_t C_GREEN     = rgb(40, 225, 70);
const uint16_t C_BLUE      = rgb(50, 120, 255);
const uint16_t C_AMBER     = rgb(230, 140, 20);

const float DEG = 0.01745329f;
const float SWEEP_FROM = -125, SWEEP_TO = 125;  // needle dials, degrees from 12 o'clock
const int STRIP_H = 20;

uint16_t mix(uint16_t a, uint16_t b, float t) {  // a at t=0, b at t=1
  if (t <= 0) return a;
  if (t >= 1) return b;
  return tft.alphaBlend((uint8_t)(t * 255), b, a);
}

uint16_t shade(uint16_t c, float k) {  // k < 1 darker, > 1 lighter
  float r = ((c >> 11) & 31) * k, g = ((c >> 5) & 63) * k, b = (c & 31) * k;
  return ((uint16_t)fminf(r, 31) << 11) | ((uint16_t)fminf(g, 63) << 5) | (uint16_t)fminf(b, 31);
}

// ================================================================ state
// What the dashboard shows; the firmware (or the preview) fills it in.
struct DashState {
  float mph = 0;          // GPS speed
  bool gpsOk = false;     // a recent fix
  float rpm = 0;
  bool indL = false, indR = false, beam = false, side = false;
  bool timeOk = false;    // the clock has been set from the GPS
  int hh = 10, mm = 8, ss = 0;
  float odo = 0, trip = 0;  // miles
};

enum LampKind : uint8_t { LAMP_LEFT, LAMP_RIGHT, LAMP_BEAM, LAMP_SIDE, LAMP_GPS, LAMP_COUNT };
enum Page : uint8_t { PAGE_DUAL, PAGE_SPEEDO, PAGE_TACHO, PAGE_CLOCK, PAGE_COUNT };
enum DialKind : uint8_t { DIAL_SPEEDO, DIAL_TACHO, DIAL_CLOCK };

// (no default values in here: the layouts below are brace-initialised, which
// C++11 only allows without them; buildFace sets the rest)
struct Dial {
  DialKind kind;
  int16_t x, y, size;   // on screen
  bool big;
  TFT_eSprite *face;
  float shown;          // needle value, easing towards the reading (-1: not yet)
  float drawnDeg;       // the needle's angle on screen
  int shownSec;         // clock: the second on screen
  float odoShown, tripShown;
  int8_t lampOn[LAMP_COUNT];  // lamps drawn in the face (round screen): what's showing
  // geometry, in face coordinates
  float c, R, bezel, Rf;  // centre, outer radius, bezel width, face radius
};

// ================================================================ geometry
float sweepAngle(float v, float vmax) { return SWEEP_FROM + (SWEEP_TO - SWEEP_FROM) * fmaxf(0, fminf(v, vmax)) / vmax; }
float px(const Dial &d, float r, float deg) { return d.c + r * sinf(deg * DEG); }
float py(const Dial &d, float r, float deg) { return d.c - r * cosf(deg * DEG); }

// The plain face colour at a point (cream with a vignette, chrome bezel,
// dash colour outside), used to draw the face and to rub out a corner of it.
uint16_t faceBase(const Dial &d, float x, float y) {
  float dx = x - d.c, dy = y - d.c, r = sqrtf(dx * dx + dy * dy);
  float inner = d.R - d.bezel;
  if (r > d.R + 0.5f) return C_DASH;
  if (r < inner - 0.5f) {
    float t = r / inner;
    uint16_t c = mix(C_FACE, C_FACE_EDGE, t * t * t);
    if (r > inner - 3.5f) c = shade(c, 0.8f + 0.2f * (inner - r) / 3.5f);  // the bezel's shadow
    return c;
  }
  // chrome: lit from the top left, a bright crest along the middle of the ring
  float a = atan2f(dy, dx);
  float light = 0.5f + 0.5f * cosf(a + 135 * DEG);
  float t = (r - inner) / d.bezel;
  float crest = sinf(fmaxf(0, fminf(t, 1)) * 3.1416f);
  float v = 0.22f + 0.5f * light * (0.4f + 0.6f * crest) + 0.25f * crest;
  uint8_t g = (uint8_t)(fminf(v, 1) * 255);
  uint16_t chrome = rgb(g, g, (uint8_t)fminf(g * 1.04f + 4, 255));
  if (r > d.R - 0.5f) return mix(chrome, C_DASH, r - (d.R - 0.5f));
  if (r < inner + 0.5f) {
    uint16_t f = shade(C_FACE_EDGE, 0.8f);
    return mix(f, chrome, r - (inner - 0.5f));
  }
  return chrome;
}

void paintBase(Dial &d, int x0, int y0, int w, int h) {
  for (int y = y0; y < y0 + h; y++)
    for (int x = x0; x < x0 + w; x++) d.face->drawPixel(x, y, faceBase(d, x, y));
}

// ================================================================ text
void useFont(TFT_eSprite &s, const uint8_t *font) {
  s.unloadFont();
  s.loadFont(font);
}

void text(Dial &d, const char *str, float x, float y, const uint8_t *font, uint16_t col, uint8_t datum = MC_DATUM) {
  useFont(*d.face, font);
  d.face->setTextDatum(datum);
  d.face->setTextColor(col, faceBase(d, x, y));
  d.face->drawString(str, (int)roundf(x), (int)roundf(y));
}

// The 7" screen's dials are much bigger than the CYD's: larger type, and
// lines scaled up with them (sk). The smaller dials keep their sizes.
bool huge(const Dial &d) { return d.Rf > 130; }
float sk(const Dial &d) { return huge(d) ? d.Rf / 91.6f : 1; }  // 91.6: the CYD's big dial
const uint8_t *numFont(const Dial &d) { return huge(d) ? LX_30 : d.big ? LX_21 : LX_15; }
const uint8_t *labelFont(const Dial &d) { return huge(d) ? LX_21 : d.big ? LX_15 : LX_11; }
const uint8_t *smallFont(const Dial &d) { return huge(d) ? LX_15 : LX_11; }

// A short tick from r0 out to r1.
void tick(Dial &d, float deg, float r0, float r1, float w, uint16_t col) {
  d.face->drawWedgeLine(px(d, r0, deg), py(d, r0, deg), px(d, r1, deg), py(d, r1, deg), w, w, col);
}

// ================================================================ odometer
// A row of drum digits: white on black, the tenths black on white.
void drawDrums(Dial &d, float cx, float cy, float miles, int whole, bool big) {
  int cw = huge(d) ? 14 : big ? 10 : 8, ch = huge(d) ? 21 : big ? 15 : 12;
  int n = whole + 1;
  int w = n * cw + 4, h = ch + 4;
  int x0 = (int)roundf(cx - w / 2.0f), y0 = (int)roundf(cy - h / 2.0f);
  d.face->fillRoundRect(x0, y0, w, h, 2, rgb(70, 66, 60));
  long tenths = (long)floorf(fmaxf(miles, 0) * 10 + 0.001f);
  long mod = 10;
  for (int i = 1; i < n; i++) mod *= 10;
  tenths %= mod;
  useFont(*d.face, huge(d) ? LX_21 : big ? LX_15 : LX_11);
  d.face->setTextDatum(MC_DATUM);
  for (int i = n - 1; i >= 0; i--) {
    int digit = tenths % 10;
    tenths /= 10;
    bool last = i == n - 1;
    int cx0 = x0 + 2 + i * cw;
    for (int r = 0; r < ch; r++) {  // the drum's curve: darker at top and bottom
      float k = 0.55f + 0.45f * sinf((r + 0.5f) / ch * 3.1416f);
      uint16_t base = last ? rgb(236, 230, 214) : rgb(34, 32, 30);
      d.face->drawFastHLine(cx0, y0 + 2 + r, cw - 1, last ? shade(base, 0.75f + 0.25f * k) : shade(base, k * 1.3f));
    }
    char s[2] = {(char)('0' + digit), 0};
    uint16_t fg = last ? C_INK : rgb(240, 236, 226);
    d.face->setTextColor(fg, last ? rgb(236, 230, 214) : rgb(44, 42, 39));
    d.face->drawString(s, cx0 + (cw - 1) / 2 + 1, y0 + 2 + ch / 2 + 1);
  }
}

// ================================================================ needle dials
#if defined(BOARD_ROUND)  // higher, to leave the bottom for the lamps
float odoY(const Dial &d) { return d.c + d.Rf * 0.24f; }
float tripY(const Dial &d) { return d.c + d.Rf * 0.44f; }
#else
float odoY(const Dial &d) { return d.c + d.Rf * (d.big ? 0.40f : 0.42f); }
float tripY(const Dial &d) { return d.c + d.Rf * 0.62f; }
#endif

void drawScale(Dial &d) {
  bool speedo = d.kind == DIAL_SPEEDO;
  float Rf = d.Rf, rt = Rf - 2;  // ticks hang from rt
  float vmax = speedo ? SPEEDO_MAX : TACHO_MAX * 1000;
  float wMajor = (d.big ? 1.3f : 1.0f) * sk(d), wMinor = (d.big ? 0.7f : 0.55f) * sk(d);

  if (!speedo) {  // red band, under the ticks
    float a0 = sweepAngle(TACHO_RED_FROM * 1000, vmax), a1 = SWEEP_TO;
    d.face->drawArc(d.c, d.c, rt, rt - Rf * 0.07f, a0 + 180, a1 + 180, C_RED, C_FACE_EDGE, true);
  }
  // the scale's edge
  d.face->drawArc(d.c, d.c, rt, rt - 1, SWEEP_FROM + 180, SWEEP_TO + 180, C_INK, C_FACE_EDGE, true);

  if (speedo) {
    for (int v = 0; v <= SPEEDO_MAX; v++) {
      float a = sweepAngle(v, vmax);
      if (v % 10 == 0) tick(d, a, rt - Rf * 0.15f, rt, wMajor, C_INK);
      else if (v % 5 == 0) tick(d, a, rt - Rf * 0.09f, rt, wMinor + 0.2f, C_INK);
      else if (d.big) tick(d, a, rt - Rf * 0.05f, rt, 0.4f * sk(d), C_INK);
    }
    for (int v = 0; v <= SPEEDO_MAX; v += 10) {
      char s[4];
      snprintf(s, sizeof s, "%d", v);
      float a = sweepAngle(v, vmax), r = Rf * (d.big ? 0.69f : 0.66f);
      text(d, s, px(d, r, a), py(d, r, a), numFont(d), C_INK);
    }
    text(d, "MPH", d.c, d.c - Rf * (d.big ? 0.22f : 0.34f), labelFont(d), C_INK);
#if SPEEDO_KMH_SCALE
    if (d.big) {  // inner km/h scale
      float ri = Rf * 0.50f;
      int kmax = (int)(SPEEDO_MAX * 1.609f);
      d.face->drawArc(d.c, d.c, ri + 2 + sk(d), ri + 2, SWEEP_FROM + 180, sweepAngle(kmax / 1.609f, vmax) + 180, C_RED, C_FACE, true);
      for (int k = 0; k <= kmax; k += 10) {
        float a = sweepAngle(k / 1.609f, vmax);
        tick(d, a, ri + 2.5f * sk(d), ri + (k % 20 ? 5.5f : 8) * sk(d), (k % 20 ? 0.45f : 0.7f) * sk(d), C_RED);
#if defined(BOARD_ROUND)
        if (k == 0) continue;  // its 0 would sit on the odometer
#endif
        if (k % 20 == 0 && k <= 140) {
          char s[4];
          snprintf(s, sizeof s, "%d", k);
          float rn = ri - 6 * sk(d);
          text(d, s, px(d, rn, a), py(d, rn, a), smallFont(d), C_RED);
        }
      }
    }
#endif
  } else {
    for (int v = 0; v <= TACHO_MAX * 4; v++) {  // quarter thousands
      float a = sweepAngle(v * 250, vmax);
      if (v % 4 == 0) tick(d, a, rt - Rf * 0.15f, rt, wMajor, C_INK);
      else if (v % 2 == 0) tick(d, a, rt - Rf * 0.09f, rt, wMinor + 0.2f, C_INK);
      else if (d.big) tick(d, a, rt - Rf * 0.05f, rt, 0.4f * sk(d), C_INK);
    }
    for (int v = 0; v <= TACHO_MAX; v++) {
      char s[3];
      snprintf(s, sizeof s, "%d", v);
      float a = sweepAngle(v * 1000, vmax), r = Rf * (d.big ? 0.69f : 0.66f);
      text(d, s, px(d, r, a), py(d, r, a), numFont(d), v >= TACHO_RED_FROM ? C_RED : C_INK);
    }
    text(d, "RPM", d.c, d.c - Rf * 0.34f, labelFont(d), C_INK);
    text(d, "x1000", d.c, d.c - Rf * 0.34f + (huge(d) ? 20 : d.big ? 14 : 11), smallFont(d), C_INK);
    text(d, "LOMAX", d.c, d.c + Rf * 0.42f, labelFont(d), shade(C_FACE_EDGE, 0.55f));
  }
}

// The parts of a speedo face that change now and then: odometer and trip.
// Drawn into the face so the strips just copy them.
bool updateSpeedoInfo(Dial &d, const DashState &st, bool force) {
  if (d.kind != DIAL_SPEEDO) return false;
  bool changed = false;
  float odo = floorf(st.odo * 10), trip = floorf(st.trip * 10);
  if (force || odo != d.odoShown) {
    drawDrums(d, d.c, odoY(d), st.odo, 5, d.big);
    d.odoShown = odo;
    changed = true;
  }
  if (d.big && (force || trip != d.tripShown)) {
    drawDrums(d, d.c, tripY(d), st.trip, 3, true);
#if !defined(BOARD_ROUND)  // no room beside it on the round dial: the trip is the short one
    text(d, "TRIP", d.c - d.Rf * 0.27f, tripY(d), smallFont(d), C_INK, MR_DATUM);
#endif
    d.tripShown = trip;
    changed = true;
  }
  return changed;
}

// ================================================================ clock
// Roman numerals drawn as strokes, standing on the rim like a pocket watch's.
// (u, v): u across the numeral, v up it (away from the centre), in units of
// the numeral's height.
struct Roman {
  Dial *d;
  float deg, r, h, thick, thin;
  float sx(float u, float v) { return px(*d, r + v * h, deg) + u * h * cosf(deg * DEG); }
  float sy(float u, float v) { return py(*d, r + v * h, deg) + u * h * sinf(deg * DEG); }
  void stroke(float u0, float v0, float u1, float v1, float w) {
    d->face->drawWedgeLine(sx(u0, v0), sy(u0, v0), sx(u1, v1), sy(u1, v1), w, w, C_INK);
  }
  void serif(float u, float v, float half) { stroke(u - half, v, u + half, v, thin); }
  // each letter at u (its left edge); returns its width
  float I(float u) {
    stroke(u + 0.09f, -0.5f, u + 0.09f, 0.5f, thick);
    serif(u + 0.09f, 0.5f, 0.13f);
    serif(u + 0.09f, -0.5f, 0.13f);
    return 0.18f;
  }
  float V(float u) {
    stroke(u + 0.06f, 0.5f, u + 0.3f, -0.5f, thick);
    stroke(u + 0.54f, 0.5f, u + 0.3f, -0.5f, thin);
    serif(u + 0.06f, 0.5f, 0.12f);
    serif(u + 0.54f, 0.5f, 0.1f);
    return 0.6f;
  }
  float X(float u) {
    stroke(u + 0.06f, 0.5f, u + 0.52f, -0.5f, thick);
    stroke(u + 0.52f, 0.5f, u + 0.06f, -0.5f, thin);
    serif(u + 0.06f, 0.5f, 0.1f);
    serif(u + 0.52f, 0.5f, 0.1f);
    serif(u + 0.06f, -0.5f, 0.1f);
    serif(u + 0.52f, -0.5f, 0.1f);
    return 0.58f;
  }
  float width(const char *s) {
    float w = 0;
    for (; *s; s++) w += (*s == 'I' ? 0.18f : *s == 'V' ? 0.6f : 0.58f) + 0.08f;
    return w - 0.08f;
  }
  void draw(const char *s) {
    float u = -width(s) / 2;
    for (; *s; s++) u += (*s == 'I' ? I(u) : *s == 'V' ? V(u) : X(u)) + 0.08f;
  }
};

void drawClockFace(Dial &d) {
  float Rf = d.Rf;
  if (!d.big) {  // small: batons only
    for (int i = 0; i < 12; i++) {
      float a = i * 30;
      if (i == 0) {
        tick(d, a - 4, Rf - 9, Rf - 2, 0.9f, C_INK);
        tick(d, a + 4, Rf - 9, Rf - 2, 0.9f, C_INK);
      } else {
        tick(d, a, Rf - (i % 3 ? 5 : 8), Rf - 2, i % 3 ? 0.6f : 1.0f, C_INK);
      }
    }
    return;
  }
  // railway minute track
  float r0 = Rf * 0.84f, r1 = Rf * 0.94f;
  d.face->drawArc(d.c, d.c, r1 + 0.5f, r1 - 0.5f, 0, 360, C_INK, C_FACE_EDGE, true);
  d.face->drawArc(d.c, d.c, r0 + 0.5f, r0 - 0.5f, 0, 360, C_INK, C_FACE_EDGE, true);
  for (int m = 0; m < 60; m++)
    tick(d, m * 6, r0, r1, m % 5 ? 0.45f : 1.1f, C_INK);
  // numerals; IIII for four, as clock makers always have
  static const char *num[12] = {"XII", "I", "II", "III", "IIII", "V", "VI", "VII", "VIII", "IX", "X", "XI"};
  float h = Rf * 0.155f;
  for (int i = 0; i < 12; i++) {
    Roman rn{&d, i * 30.0f, r0 - h * 0.8f, h, h * 0.08f, h * 0.035f};
    rn.draw(num[i]);
  }
  text(d, "LOMAX", d.c, d.c - Rf * 0.36f, smallFont(d), C_INK);
}

void buildFace(Dial &d) {
  d.c = d.size / 2;  // whole pixels, so drawArc's rings line up with the ticks
  d.R = d.c - 1;
  d.bezel = fmaxf(3, d.R * 0.075f);
  d.Rf = d.R - d.bezel;
  paintBase(d, 0, 0, d.size, d.size);
  if (d.kind == DIAL_CLOCK) drawClockFace(d);
  else drawScale(d);
  d.shown = -1;
  d.drawnDeg = 999;
  d.shownSec = -1;
  d.odoShown = d.tripShown = -1;
}

// ================================================================ strips
TFT_eSprite *strip = nullptr;
int stripW = 0;

// drawWedgeLine scans from one end of the line, so a long needle clipped to
// a 20-row strip would still walk all its rows. Cut it to the strip first.
void wedge(float ax, float ay, float bx, float by, float ar, float br, uint16_t col, int oy) {
  ay -= oy;
  by -= oy;
  float m = fmaxf(ar, br) + 2, lo = -m, hi = STRIP_H + m;
  if ((ay < lo && by < lo) || (ay > hi && by > hi)) return;
  if (fabsf(by - ay) > 0.01f) {
    float t0 = (lo - ay) / (by - ay), t1 = (hi - ay) / (by - ay);
    if (t0 > t1) { float t = t0; t0 = t1; t1 = t; }
    t0 = fmaxf(t0, 0);
    t1 = fminf(t1, 1);
    float nax = ax + (bx - ax) * t0, nay = ay + (by - ay) * t0, nar = ar + (br - ar) * t0;
    float nbx = ax + (bx - ax) * t1, nby = ay + (by - ay) * t1, nbr = ar + (br - ar) * t1;
    ax = nax; ay = nay; ar = nar; bx = nbx; by = nby; br = nbr;
  }
  strip->drawWedgeLine(ax, ay, bx, by, ar, br, col);
}

void spot(float x, float y, float r, uint16_t col, int oy) {
  if (y - oy < -r - 1 || y - oy > STRIP_H + r + 1) return;
  strip->drawSpot(x, y - oy, r, col);
}

// a needle from the tail through the centre to the tip, with its shadow
void drawNeedle(Dial &d, float deg, int oy) {
  float Rf = d.Rf, tail = Rf * 0.2f, tip = Rf * 0.9f;
  float wb = (d.big ? 2.6f : 2.0f) * sk(d), wt = 0.7f * sk(d);
  for (int pass = 0; pass < 2; pass++) {
    float o = pass ? 0 : (d.big ? 2.5f : 2) * sk(d);  // shadow first, down and right
    uint16_t col = pass ? C_NEEDLE : shade(C_FACE_EDGE, 0.7f);
    wedge(px(d, -tail, deg) + o, py(d, -tail, deg) + o, d.c + o, d.c + o, wb * 0.8f, wb, col, oy);
    wedge(d.c + o, d.c + o, px(d, tip, deg) + o, py(d, tip, deg) + o, wb, wt, col, oy);
  }
  float cap = Rf * 0.1f;
  spot(d.c, d.c, cap, rgb(28, 26, 24), oy);
  spot(d.c - cap * 0.25f, d.c - cap * 0.25f, cap * 0.45f, rgb(110, 106, 100), oy);
}

// a Breguet hand: a slim shaft, an open "moon" near the end, a fine point
void drawBreguet(Dial &d, float deg, float len, float moonAt, float moonR, float w, uint16_t col, int oy) {
  float c = d.c;
  wedge(px(d, -len * 0.12f, deg), py(d, -len * 0.12f, deg), c, c, w * 0.8f, w, col, oy);
  wedge(c, c, px(d, moonAt - moonR * 0.8f, deg), py(d, moonAt - moonR * 0.8f, deg), w, w * 0.55f, col, oy);
  float mx = px(d, moonAt, deg), my = py(d, moonAt, deg);
  spot(mx, my, moonR, col, oy);
  spot(mx, my, moonR * 0.52f, faceBase(d, mx, my), oy);
  wedge(px(d, moonAt + moonR * 0.8f, deg), py(d, moonAt + moonR * 0.8f, deg), px(d, len, deg), py(d, len, deg),
        w * 0.6f, 0.3f, col, oy);
}

void drawHands(Dial &d, const DashState &st, int oy) {
  float Rf = d.Rf;
  float hDeg = ((st.hh % 12) + st.mm / 60.0f) * 30, mDeg = (st.mm + st.ss / 60.0f) * 6, sDeg = st.ss * 6;
  uint16_t hand = st.timeOk ? C_BLUED : shade(C_FACE_EDGE, 0.75f);
  if (d.big) {
    drawBreguet(d, hDeg, Rf * 0.55f, Rf * 0.42f, Rf * 0.075f, 2.2f * sk(d), hand, oy);
    drawBreguet(d, mDeg, Rf * 0.84f, Rf * 0.66f, Rf * 0.06f, 1.8f * sk(d), hand, oy);
  } else {
    wedge(d.c, d.c, px(d, Rf * 0.55f, hDeg), py(d, Rf * 0.55f, hDeg), 1.6f, 0.6f, hand, oy);
    wedge(d.c, d.c, px(d, Rf * 0.85f, mDeg), py(d, Rf * 0.85f, mDeg), 1.3f, 0.4f, hand, oy);
  }
  if (st.timeOk) {  // seconds, with a counterweight
    float tail = Rf * (d.big ? 0.24f : 0.2f), tip = Rf * 0.9f;
    uint16_t col = rgb(150, 24, 20);
    wedge(px(d, -tail, sDeg), py(d, -tail, sDeg), px(d, tip, sDeg), py(d, tip, sDeg), (d.big ? 0.8f : 0.5f) * sk(d), 0.35f * sk(d), col, oy);
    if (d.big) spot(px(d, -tail, sDeg), py(d, -tail, sDeg), 3 * sk(d), col, oy);
  }
  spot(d.c, d.c, (d.big ? 4 : 2.2f) * sk(d), C_BRASS, oy);
}

void renderDial(Dial &d, const DashState &st) {
  const uint16_t *src = (const uint16_t *)d.face->getPointer();
  uint16_t *dst = (uint16_t *)strip->getPointer();
  for (int oy = 0; oy < d.size; oy += STRIP_H) {
    int h = d.size - oy < STRIP_H ? d.size - oy : STRIP_H;
    for (int r = 0; r < h; r++) memcpy(dst + r * stripW, src + (oy + r) * d.size, d.size * 2);
    if (d.kind == DIAL_CLOCK) drawHands(d, st, oy);
    else drawNeedle(d, sweepAngle(d.shown, d.kind == DIAL_SPEEDO ? SPEEDO_MAX : TACHO_MAX * 1000), oy);
    // only the round part, so dials can sit closer than their squares allow
    for (int r = 0; r < h; r++) {
      float dy = oy + r - d.c, half = (d.R + 1) * (d.R + 1) - dy * dy;
      if (half <= 0) continue;
      half = sqrtf(half);
      int x0 = (int)fmaxf(0, floorf(d.c - half)), x1 = (int)fminf(d.size - 1, ceilf(d.c + half));
      screenPush(*strip, x0, r, x1 - x0 + 1, 1, d.x + x0, d.y + oy + r);
    }
  }
}

// ================================================================ lamps
// Tell-tales, drawn by sampling each pixel 4 x 4 times against the shape.
const int LAMP_W = 34, LAMP_H = 26;  // the shapes' design size
#if defined(BOARD_7INCH)
const float LAMP_SCALE = 1.6f;       // lamps around the dials, on the screen
#else
const float LAMP_SCALE = 1;
#endif
const int LW = (int)(LAMP_W * LAMP_SCALE + 0.99f), LH = (int)(LAMP_H * LAMP_SCALE + 0.99f);
TFT_eSprite *lampSpr = nullptr;

bool inLamp(LampKind k, float x, float y) {
  // centred coordinates, y down, roughly -12..12
  x -= LAMP_W / 2.0f;
  y -= LAMP_H / 2.0f;
  switch (k) {
    case LAMP_RIGHT: x = -x;  // fall through: the left arrow mirrored
    case LAMP_LEFT: {
      // head: triangle, point at x=-14, back at x=-2, half height 10
      if (x >= -14 && x <= -2 && fabsf(y) <= (x + 14) * (10.0f / 12)) return true;
      return x > -2.5f && x <= 13 && fabsf(y) <= 4.2f;  // stem
    }
    case LAMP_BEAM: {
      // a headlamp: a solid D, its flat lens to the left, five rays leaving it
      if (x >= 0 && x * x + y * y <= 10.0f * 10.0f) return true;
      for (int i = -2; i <= 2; i++)
        if (fabsf(y - i * 4.2f) <= 1.1f && x >= -14 && x <= -3) return true;
      return false;
    }
    case LAMP_SIDE: {
      // two small lamps, rounded backs together, lenses facing out, three
      // splayed rays from each
      float ax = fabsf(x), dx = 6.5f - ax;
      if (ax <= 6.5f && dx * dx + y * y <= 5.5f * 5.5f) return true;
      for (int i = -1; i <= 1; i++) {
        float t = ax - 8.5f;
        if (t >= 0 && t <= 6 && fabsf(y - i * (4.0f + t * 0.4f)) <= 1.0f) return true;
      }
      return false;
    }
    default:
      return false;
  }
}

// a lamp drawn into a dial's face (round screen), centred on (fx, fy) in
// units of the face radius from its centre, at 0.72 of the full size
const float FACE_LAMP_SCALE = 0.72f;

void drawFaceLamp(Dial &d, LampKind k, bool on, float fx, float fy) {
  const float sc = FACE_LAMP_SCALE;
  int w = (int)ceilf(LAMP_W * sc), h = (int)ceilf(LAMP_H * sc);
  int x0 = (int)roundf(d.c + fx * d.Rf - w / 2.0f), y0 = (int)roundf(d.c + fy * d.Rf - h / 2.0f);
  // darker colours than on the black panel, to stand out on cream
  uint16_t lit = k == LAMP_BEAM ? rgb(20, 80, 225) : k == LAMP_GPS ? rgb(215, 110, 0) : rgb(10, 165, 45);
  if (k == LAMP_GPS) {
    paintBase(d, x0, y0, w, h);
    uint16_t col = on ? lit : shade(faceBase(d, x0 + w / 2, y0 + h / 2), 0.88f);
    text(d, "GPS", x0 + w / 2.0f, y0 + h / 2.0f + 1, LX_11, col);
    d.face->drawRoundRect(x0, y0 + 2, w, h - 4, 3, col);
    return;
  }
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      int n = 0;
      for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++) n += inLamp(k, (x + (sx + 0.5f) / 4) / sc, (y + (sy + 0.5f) / 4) / sc);
      uint16_t base = faceBase(d, x0 + x, y0 + y);
      uint16_t col = on ? lit : shade(base, 0.88f);  // unlit: a faint shadow of the symbol
      d.face->drawPixel(x0 + x, y0 + y, mix(base, col, n / 16.0f));
    }
}

void drawLamp(LampKind k, bool on, int x, int y) {
  uint16_t col = !on ? C_LAMP_OFF : (k == LAMP_BEAM ? C_BLUE : k == LAMP_GPS ? C_AMBER : C_GREEN);
  if (k == LAMP_GPS) {  // lit while there's no fix, so the speedo can't be trusted
    lampSpr->fillSprite(C_DASH);
    lampSpr->loadFont(LAMP_SCALE > 1 ? LX_21 : LX_15);
    lampSpr->setTextDatum(MC_DATUM);
    lampSpr->setTextColor(col, C_DASH);
    lampSpr->drawString("GPS", LW / 2, LH / 2 + 1);
    lampSpr->unloadFont();
    int m = (int)(3 * LAMP_SCALE);
    lampSpr->drawRoundRect(1, m, LW - 2, LH - 2 * m, 4, col);
    if (LAMP_SCALE > 1) lampSpr->drawRoundRect(2, m + 1, LW - 4, LH - 2 * m - 2, 3, col);
    screenPush(*lampSpr, 0, 0, LW, LH, x, y);
    return;
  }
  for (int py = 0; py < LH; py++)
    for (int px = 0; px < LW; px++) {
      int n = 0;
      for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++)
          n += inLamp(k, (px + (sx + 0.5f) / 4) / LAMP_SCALE, (py + (sy + 0.5f) / 4) / LAMP_SCALE);
      lampSpr->drawPixel(px, py, mix(C_DASH, col, n / 16.0f));
    }
  screenPush(*lampSpr, 0, 0, LW, LH, x, y);
}

// ================================================================ pages
struct LampPos { int16_t x, y; };      // on the screen, top-left
struct FaceLampPos { float x, y; };    // in the dial's face, see drawFaceLamp
struct PageLayout {
  uint8_t n;                           // dials; 0: no such page on this screen
  Dial dials[3];
  bool lampsInFace;                    // in dials[0]'s face, not around it
  LampPos lamp[LAMP_COUNT];            // left, right, beam, side, GPS
  FaceLampPos faceLamp[LAMP_COUNT];
};

#if defined(BOARD_ROUND)
// one dial filling the round screen; the lamps in a row below the needle's
// pivot, or around the clock's centre
#define ROW_LAMPS {{-0.48f, 0.7f}, {0.48f, 0.7f}, {0.24f, 0.7f}, {-0.24f, 0.7f}, {0, 0.7f}}
PageLayout layouts[PAGE_COUNT] = {
    {0},
    {1, {{DIAL_SPEEDO, 0, 0, 240, true}}, true, {}, ROW_LAMPS},
    {1, {{DIAL_TACHO, 0, 0, 240, true}}, true, {}, ROW_LAMPS},
    {1, {{DIAL_CLOCK, 0, 0, 240, true}}, true, {}, {{-0.42f, 0}, {0.42f, 0}, {0.3f, 0.4f}, {-0.3f, 0.4f}, {0, 0.4f}}},
};
#elif defined(BOARD_7INCH)
// one page: a big speedo and rev counter, the clock between them at the top,
// the indicators and lamps in the top corners, the GPS lamp low in the middle
PageLayout layouts[PAGE_COUNT] = {
    {3, {{DIAL_SPEEDO, 14, 126, 340, true}, {DIAL_TACHO, 446, 126, 340, true}, {DIAL_CLOCK, 300, 4, 200, true}},
     false, {{18, 16}, {728, 16}, {650, 16}, {96, 16}, {373, 420}}},
    {0}, {0}, {0},
};
#else
// side by side: speedo and tacho, a small clock between them at the top
// big pages: one 200 px dial, the lamps in the corners
PageLayout layouts[PAGE_COUNT] = {
    {3, {{DIAL_SPEEDO, 2, 62, 156, false}, {DIAL_TACHO, 162, 62, 156, false}, {DIAL_CLOCK, 128, 2, 64, false}},
     false, {{4, 4}, {282, 4}, {234, 4}, {52, 4}, {143, 211}}},
    {1, {{DIAL_SPEEDO, 60, 20, 200, true}}, false, {{8, 8}, {278, 8}, {8, 206}, {278, 206}, {8, 107}}},
    {1, {{DIAL_TACHO, 60, 20, 200, true}}, false, {{8, 8}, {278, 8}, {8, 206}, {278, 206}, {8, 107}}},
    {1, {{DIAL_CLOCK, 60, 20, 200, true}}, false, {{8, 8}, {278, 8}, {8, 206}, {278, 206}, {8, 107}}},
};
#endif

// the page after p that this screen has
Page nextPage(Page p) {
  do p = (Page)((p + 1) % PAGE_COUNT);
  while (layouts[p].n == 0);
  return p;
}

bool hasPage(int p) { return p >= 0 && p < PAGE_COUNT && layouts[p].n > 0; }

Page page = PAGE_DUAL;
bool lampShown[LAMP_COUNT];

PageLayout &L() { return layouts[page]; }

void freePage() {
  for (auto &pl : layouts)
    for (int i = 0; i < 3; i++)
      if (pl.dials[i].face) { pl.dials[i].face->deleteSprite(); delete pl.dials[i].face; pl.dials[i].face = nullptr; }
  if (strip) { strip->deleteSprite(); delete strip; strip = nullptr; }
}

void lampStates(const DashState &st, bool *on) {
  on[LAMP_LEFT] = st.indL;
  on[LAMP_RIGHT] = st.indR;
  on[LAMP_BEAM] = st.beam;
  on[LAMP_SIDE] = st.side;
  on[LAMP_GPS] = !st.gpsOk;  // lit while there's no fix, so the speedo can't be trusted
}

// lamps in a dial's face; true if any changed (the dial needs redrawing)
bool updateFaceLamps(Dial &d, const DashState &st, bool force) {
  if (!L().lampsInFace || &d != &L().dials[0]) return false;
  bool on[LAMP_COUNT], changed = false;
  lampStates(st, on);
  for (int i = 0; i < LAMP_COUNT; i++)
    if (force || on[i] != d.lampOn[i]) {
      drawFaceLamp(d, (LampKind)i, on[i], L().faceLamp[i].x, L().faceLamp[i].y);
      d.lampOn[i] = on[i];
      changed = true;
    }
  return changed;
}

void drawLamps(const DashState &st, bool force) {
  if (L().lampsInFace) return;
  bool on[LAMP_COUNT];
  lampStates(st, on);
  for (int i = 0; i < LAMP_COUNT; i++)
    if (force || on[i] != lampShown[i]) {
      drawLamp((LampKind)i, on[i], L().lamp[i].x, L().lamp[i].y);
      lampShown[i] = on[i];
    }
}

void showPage(Page p, const DashState &st) {
  freePage();
  page = p;
  screenFill(0, 0, SCR_W, SCR_H, C_DASH);
  stripW = 0;
  for (int i = 0; i < L().n; i++) {
    Dial &d = L().dials[i];
    d.face = new TFT_eSprite(&tft);
    d.face->setColorDepth(16);
    if (!d.face->createSprite(d.size, d.size)) Serial.printf("no memory for a %d px dial\n", d.size);
    buildFace(d);
    updateSpeedoInfo(d, st, true);
    updateFaceLamps(d, st, true);
    if (d.size > stripW) stripW = d.size;
  }
  strip = new TFT_eSprite(&tft);
  strip->setColorDepth(16);
  strip->createSprite(stripW, STRIP_H);
  if (!lampSpr) {
    lampSpr = new TFT_eSprite(&tft);
    lampSpr->setColorDepth(16);
    lampSpr->createSprite(LW, LH);
  }
  drawLamps(st, true);
}

// Move the needles towards the readings and redraw what changed.
// dt in seconds; snap jumps straight to the readings.
void updateDash(const DashState &st, float dt, bool snap = false) {
  drawLamps(st, false);
  for (int i = 0; i < L().n; i++) {
    Dial &d = L().dials[i];
    if (!d.face || !d.face->created()) continue;
    if (d.kind == DIAL_CLOCK) {
      int sec = st.timeOk ? st.hh * 3600 + st.mm * 60 + st.ss : -2;
      bool lamps = updateFaceLamps(d, st, false);
      if (sec != d.shownSec || lamps) { d.shownSec = sec; renderDial(d, st); }
      continue;
    }
    bool info = updateSpeedoInfo(d, st, false);
    info |= updateFaceLamps(d, st, false);
    float target = d.kind == DIAL_SPEEDO ? st.mph : st.rpm;
    float vmax = d.kind == DIAL_SPEEDO ? SPEEDO_MAX : TACHO_MAX * 1000;
    // the needle eases in: GPS speed comes once a second (or 5 times)
    float k = d.kind == DIAL_SPEEDO ? 5.0f : 10.0f;
    d.shown = (snap || d.shown < 0) ? target : d.shown + (target - d.shown) * fminf(1, dt * k);
    float deg = sweepAngle(d.shown, vmax);
    if (info || fabsf(deg - d.drawnDeg) > 0.12f) {
      d.drawnDeg = deg;
      renderDial(d, st);
    }
  }
}

// ================================================================ start-up
// The logo: the winged Lomax badge if its header is there, otherwise the
// name on a chrome-edged enamel badge (oval, or round on the round screen).
void drawSplash() {
  screenFill(0, 0, SCR_W, SCR_H, C_DASH);
#ifdef HAVE_LOMAX_LOGO
  TFT_eSprite s(&tft);
  s.setColorDepth(16);
  if (s.createSprite(LOMAX_LOGO_W, LOMAX_LOGO_H)) {
    s.setSwapBytes(true);  // the picture is plain RGB565; sprites keep theirs byte-swapped
    s.pushImage(0, 0, LOMAX_LOGO_W, LOMAX_LOGO_H, lomax_logo_bitmap);
    screenPush(s, 0, 0, LOMAX_LOGO_W, LOMAX_LOGO_H, (SCR_W - LOMAX_LOGO_W) / 2, (SCR_H - LOMAX_LOGO_H) / 2);
    s.deleteSprite();
  }
#else
#if defined(BOARD_ROUND)
  const int W = 236, H = 236;  // a round badge
#else
  const int W = 280, H = 120;  // an oval one
#endif
  TFT_eSprite s(&tft);
  s.setColorDepth(16);
  if (!s.createSprite(W, H)) return;
  float cx = W / 2.0f - 0.5f, cy = H / 2.0f - 0.5f, a = W / 2.0f - 2, b = H / 2.0f - 2;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      // distance from the oval's edge, roughly in pixels
      float nx = (x - cx) / a, ny = (y - cy) / b, e = sqrtf(nx * nx + ny * ny);
      float dist = (1 - e) * b;  // > 0 inside
      uint16_t c = C_DASH;
      if (dist > -0.5f) {
        float light = 0.5f - 0.5f * ny;
        uint8_t g = (uint8_t)(70 + 150 * light);
        uint16_t chrome = rgb(g, g, g + 6 > 255 ? 255 : g + 6);
        uint16_t inner = rgb(110, 22, 18);  // deep red enamel
        if (dist < 7) c = mix(C_DASH, chrome, fminf(1, dist + 0.5f));
        else c = mix(shade(inner, 0.7f + 0.3f * light), chrome, fmaxf(0, 8 - dist));
        if (dist > 10.5f && dist < 12) c = mix(c, rgb(214, 190, 120), 0.6f);  // a fine gold line
      }
      s.drawPixel(x, y, c);
    }
  s.loadFont(LX_48);
  s.setTextDatum(MC_DATUM);
  s.setTextColor(rgb(245, 236, 210), rgb(110, 22, 18));
  s.drawString("LOMAX", W / 2, H / 2 + 3);
  s.unloadFont();
  screenPush(s, 0, 0, W, H, (SCR_W - W) / 2, (SCR_H - H) / 2);
  s.deleteSprite();
#endif
}
