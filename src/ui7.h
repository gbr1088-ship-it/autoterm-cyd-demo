// 7" screen (480x800, upright): the header, tab bar, heater page and
// utilities page, drawn like the phone app (web/app.html is the design
// source: its colours, cards, gauge, stepper, mode selector and power
// button). Included by main.cpp in place of the CYD's versions of these
// functions, so the shared logic calls the same names on both boards.
// The settings, system, phone and WiFi pages are main.cpp's own, laid out
// with the 7" constants there; this file adds the header and tabs to them.

#pragma once

const uint16_t C_LINE = rgb(58, 47, 39);  // the app's --line: card and pill borders

TFT_eSprite pillSpr(&tft);    // state pill under the gauge
TFT_eSprite statSpr(&tft);    // a value inside a stat card (shared by the three)
TFT_eSprite setvalSpr(&tft);  // the setpoint card between - and +
TFT_eSprite voltSpr(&tft);    // voltage pill in the header
TFT_eSprite chainSpr(&tft);   // windlass: metres of chain out

extern uint8_t phoneClients;  // phone app section of main.cpp
void drawTabs();

void ui7Begin() {
  anim.createSprite(44, 36);
  pillSpr.createSprite(300, 32);
  statSpr.createSprite(124, 30);
  setvalSpr.createSprite(btnPlus.x - (btnMinus.x + btnMinus.w) - 16, btnMinus.h);
  voltSpr.createSprite(96, 36);
  chainSpr.createSprite(WIN_W - 32, 56);
}

// ---------------------------------------------------------------- text
// All text is in the smooth fonts of fonts7.h, at the app's pixel sizes.
// A function that loads another size on tft puts the body font (F7_15)
// back when it's done, since main.cpp's pages expect it.
// Uppercase labels are letter-spaced as in the app. These use whatever font
// is loaded on g and draw left to right from x.
int charWidth(TFT_eSPI &g, char ch) {
  char c[2] = {ch == ' ' ? 't' : ch, 0};  // a lone space measures 0 wide
  return g.textWidth(c);
}

int spacedWidth(TFT_eSPI &g, const char *s, int sp) {
  int w = 0;
  for (; *s; s++) w += charWidth(g, *s) + sp;
  return w > 0 ? w - sp : 0;
}

int drawSpaced(TFT_eSPI &g, const char *s, int x, int y, int sp) {
  char c[2] = {0, 0};
  g.setTextDatum(ML_DATUM);
  for (; *s; s++) {
    c[0] = *s;
    if (*s != ' ') g.drawString(c, x, y);
    x += charWidth(g, *s) + sp;
  }
  return x;
}

void drawSpacedCentre(TFT_eSPI &g, const char *s, int cx, int y, int sp) {
  drawSpaced(g, s, cx - spacedWidth(g, s, sp) / 2, y, sp);
}

// 13 px text wrapped onto lines no wider than w; returns the y after it.
int drawWrapped(const char *s, int x, int y, int w, int lineH) {
  char line[96];
  tft.loadFont(F7_13);
  tft.setTextDatum(TL_DATUM);
  while (*s) {
    int n = 0, lastSpace = -1;
    while (s[n] && n < (int)sizeof line - 1) {
      line[n] = s[n];
      line[n + 1] = 0;
      if (s[n] == ' ') lastSpace = n;
      if (tft.textWidth(line) > w && lastSpace > 0) { n = lastSpace; break; }
      n++;
    }
    line[n] = 0;
    tft.drawString(line, x, y);
    s += n;
    while (*s == ' ') s++;
    y += lineH;
  }
  setBodyFont(tft);
  return y;
}

// fillTriangle has hard edges; going round it with anti-aliased lines softens them
void fillSmoothTriangle(TFT_eSPI &g, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t col, uint16_t bg) {
  g.fillTriangle(x0, y0, x1, y1, x2, y2, col);
  g.drawWideLine(x0, y0, x1, y1, 1.5f, col, bg);
  g.drawWideLine(x1, y1, x2, y2, 1.5f, col, bg);
  g.drawWideLine(x2, y2, x0, y0, 1.5f, col, bg);
}

// ---------------------------------------------------------------- header and tabs
// The logo tile, ADVENTURE VAN LIFE, a status line under it (what the CYD
// shows in its header pill) and the voltage on the right.
extern bool voltShown;

void drawHeader(bool tabs) {
  tft.fillRect(0, 0, SCR_W, HDR_H, C_BG);
  voltShown = false;
  tft.pushImage(16, (HDR_H - AVL_LOGO_SMALL_H) / 2, AVL_LOGO_SMALL_W, AVL_LOGO_SMALL_H, avl_logo_small_bitmap);
  tft.loadFont(F7_17);
  tft.setTextColor(C_TEXT, C_BG);
  int x = drawSpaced(tft, "ADVENTURE", 78, 21, 1);
  tft.setTextColor(C_ORANGE, C_BG);
  drawSpaced(tft, "VAN LIFE", x + 6, 21, 3);
  setBodyFont(tft);
  tft.drawFastHLine(0, HDR_H - 1, SCR_W, tft.alphaBlend(77, C_ORANGE, C_TRACK));
  if (tabs) drawTabs();
}

// the status line under the title: a coloured dot and the text
void drawPill(const char *text, uint16_t col) {
  tft.fillRect(76, 34, 290, 22, C_BG);
  tft.fillSmoothCircle(83, 45, 4, col, C_BG);
  tft.loadFont(F7_13);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(col == C_RED ? C_RED : C_DIM, C_BG);
  tft.drawString(text, 94, 45);
  setBodyFont(tft);
}

// The voltage in a pill on the right of the header. Called often, so it
// only draws when the reading or the page changed: every redraw of the
// screen is time the panel shares memory with the processor.
bool voltShown = false;  // cleared when the header is drawn again

void drawVoltage() {
  static int last = -1;
  int v = lroundf(voltage * 10);
  if (voltShown && v == last) return;
  voltShown = true;
  last = v;
  char buf[10];
  snprintf(buf, sizeof buf, "%.1f", voltage);
  TFT_eSprite &s = voltSpr;
  const int h = s.height();
  s.fillSprite(C_BG);
  s.fillSmoothRoundRect(0, 0, s.width(), h, h / 2, C_PANEL, C_BG);
  s.loadFont(F7_13);
  int vw = s.textWidth("V");
  s.loadFont(F7_17);
  int x = (s.width() - (s.textWidth(buf) + 2 + vw)) / 2;
  s.setTextDatum(L_BASELINE);
  s.setTextColor(voltage < 11.8f ? C_RED : C_TEXT, C_PANEL);
  s.drawString(buf, x, h / 2 + 6);
  x += s.textWidth(buf) + 2;
  s.loadFont(F7_13);
  s.setTextColor(C_DIM, C_PANEL);
  s.drawString("V", x, h / 2 + 6);
  pushSpr(s, SCR_W - 16 - s.width(), (HDR_H - h) / 2);
}

void drawTabIcon(int i, int cx, int cy, uint16_t col) {
  switch (i) {
    case 0:  // flame
      tft.fillSmoothCircle(cx, cy + 4, 7, col, C_BG);
      fillSmoothTriangle(tft, cx - 7, cy + 3, cx + 7, cy + 3, cx + 1, cy - 11, col, C_BG);
      break;
    case 1:  // switch
      tft.drawSmoothRoundRect(cx - 11, cy - 6, 6, 5, 22, 12, col, C_BG);
      tft.fillSmoothCircle(cx + 5, cy, 3, col, C_BG);
      break;
    case 2: {  // phone, with a dot: orange while its network is open, green with a phone on it
      tft.fillSmoothRoundRect(cx - 7, cy - 11, 14, 22, 3, col, C_BG);
      tft.fillRect(cx - 5, cy - 8, 10, 15, C_BG);
      if (phoneUp) tft.fillSmoothCircle(cx + 9, cy - 9, 4, phoneClients ? C_GREEN : C_ORANGE, C_BG);
      break;
    }
    case 3:  // gear
      for (int k = 0; k < 8; k++) {
        float a = k * PI / 4;
        tft.drawWideLine(cx + cosf(a) * 6, cy + sinf(a) * 6, cx + cosf(a) * 10, cy + sinf(a) * 10, 3, col, C_BG);
      }
      tft.fillSmoothCircle(cx, cy, 7, col, C_BG);
      tft.fillSmoothCircle(cx, cy, 3, C_BG, col);
      break;
  }
}

// Heating, Utilities, Phone, Settings; the current one in orange with a bar on top
void drawTabs() {
  if (page == PAGE_SAVER || page == PAGE_WIFI) return;
  int sel = page == PAGE_UTIL ? 1 : page == PAGE_PHONE ? 2 : (page == PAGE_SETTINGS || page == PAGE_SYSTEM) ? 3 : 0;
  const Txt names[4] = {T_TAB_HEAT, T_TAB_UTIL, T_TAB_PHONE, T_TAB_SETTINGS};
  tft.fillRect(0, TAB_Y, SCR_W, SCR_H - TAB_Y, C_BG);
  tft.drawFastHLine(0, TAB_Y, SCR_W, C_TRACK);
  tft.loadFont(F7_13);
  tft.setTextDatum(MC_DATUM);
  for (int i = 0; i < 4; i++) {
    const Button &b = btnTab[i];
    uint16_t col = i == sel ? C_ORANGE : C_DIM;
    int cx = b.x + b.w / 2;
    if (i == sel) tft.fillSmoothRoundRect(b.x + b.w * 3 / 10, TAB_Y - 2, b.w * 2 / 5, 5, 2, C_ORANGE, C_BG);
    drawTabIcon(i, cx, TAB_Y + 22, col);
    tft.setTextColor(col, C_BG);
    tft.drawString(tr(names[i]), cx, TAB_Y + 47);
  }
  setBodyFont(tft);
}

// ---------------------------------------------------------------- heater page
const int G_OUT = GR + 9, G_IN = GR - 9;  // the ring (the app's r 112, stroke 16, at 1.13x)

void gaugePoint(float t, float r, float &x, float &y) {  // t: 0..35 C along the sweep
  float a = (-130 + t / G_MAX_T * 260) * DEG_TO_RAD;  // 0 = straight up, as in the app
  x = GX + sinf(a) * r;
  y = GY - cosf(a) * r;
}

// ticks every 5 C and the 0 / 10 / 20 / 30 labels, inside the ring
void drawGaugeScale() {
  tft.loadFont(F7_13);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_DIM, C_BG);
  for (int t = 0; t <= 35; t += 5) {
    float x0, y0, x1, y1;
    gaugePoint(t, G_IN - 7, x0, y0);
    gaugePoint(t, G_IN - (t % 10 ? 11 : 16), x1, y1);
    tft.drawWideLine(x0, y0, x1, y1, 2, C_DIM, C_BG);
    if (t % 10 == 0) {
      char buf[4];
      snprintf(buf, sizeof buf, "%d", t);
      gaugePoint(t, G_IN - 30, x0, y0);
      tft.drawString(buf, x0, y0);
    }
  }
  setBodyFont(tft);
}

// 52 segments lit up to the cabin temperature, peach -> orange -> red; the
// setpoint as a white marker outside the ring (temperature mode only)
void drawGauge() {
  const int SEGS = 52;
  float f = constrain((cabinTemp - G_MIN_T) / (G_MAX_T - G_MIN_T), 0.0f, 1.0f);
  int lit = lroundf(f * SEGS);
  for (int i = 0; i < SEGS; i++) {
    int a = G_START + i * 5;
    // smooth (anti-aliased) edges against the background
    tft.drawArc(GX, GY, G_OUT, G_IN, a, a + 4, i < lit ? heatColor((float)i / (SEGS - 1)) : C_TRACK, C_BG, true);
  }
  tft.drawArc(GX, GY, G_OUT + 18, G_OUT + 1, G_START - 12, G_END + 12, C_BG, C_BG, false);  // old marker
  if (mode != MODE_TEMP) return;
  // a smooth wedge pointing in at the ring
  float t = constrain(setTemp, 0, 35), x0, y0, x1, y1;
  gaugePoint(t, G_OUT + 4, x0, y0);
  gaugePoint(t, G_OUT + 15, x1, y1);
  tft.drawWedgeLine(x0, y0, x1, y1, 0.5f, 6.5f, C_TEXT, C_BG);
}

// the big cabin temperature with a small degree C after it
void drawTemperature() {
  char buf[12];
  snprintf(buf, sizeof buf, "%.1f", cabinTemp);
  TFT_eSprite &s = tempSpr;
  s.fillSprite(C_BG);
  setHugeFont(s);
  s.setTextDatum(L_BASELINE);
  s.setTextColor(C_TEXT, C_BG);
  int w = s.textWidth(buf);
  int x = (s.width() - w) / 2 - 12, base = s.height() - 12;
  s.drawString(buf, x, base);
  // superscript degree C, as the app's 22 px sup
  drawDegree(s, x + w + 8, TEMP_DEG_Y, 4, C_DIM, C_BG);
  s.loadFont(F7_20);
  s.setTextDatum(TL_DATUM);
  s.setTextColor(C_DIM, C_BG);
  s.drawString("C", x + w + 14, TEMP_DEG_Y - 5);
  pushSpr(tempSpr, GX - tempSpr.width() / 2, GY - 10 - tempSpr.height() / 2);
  drawGaugeScale();  // the sprite's corners cover the 10 and 30 labels
}

// CABIN . PROBE / SIMULATED under the temperature
void drawCabinLabel() {
  const int y = GY + 46;
  tft.fillRect(GX - 100, y - 9, 200, 18, C_BG);
  const char *a = tr(T_CABIN), *b = tr(sensorOk ? T_PROBE : T_SIMULATED);
  tft.loadFont(F7_11);
  const int gap = 16;  // with a middle dot in it (the font has none)
  int w = spacedWidth(tft, a, 2) + gap + spacedWidth(tft, b, 2);
  tft.setTextColor(C_DIM, C_BG);
  int x = drawSpaced(tft, a, GX - w / 2, y, 2);
  tft.fillSmoothCircle(x - 2 + gap / 2, y, 1, C_DIM, C_BG);
  tft.setTextColor(C_TEXT, C_BG);
  drawSpaced(tft, b, x - 2 + gap, y, 2);
  setBodyFont(tft);
}

const char *fmtTime(uint32_t ms, char *buf, size_t n) {
  uint32_t s = ms / 1000;
  snprintf(buf, n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
  return buf;
}

// state pill under the gauge: a dot, the state and how long it's been in it
void drawGaugePill() {
  char tbuf[12] = "";
  const char *txt = showNoLink() ? tr(T_NO_LINK) : tr(stateText(state));
  uint16_t col = showNoLink() ? C_RED : STATE_COLOR[state];
  if (state != ST_OFF && !showNoLink()) fmtTime(millis() - stateSince, tbuf, sizeof tbuf);
  TFT_eSprite &s = pillSpr;
  s.fillSprite(C_BG);
  s.loadFont(F7_13);
  int timeW = tbuf[0] ? s.textWidth(tbuf) + 10 : 0;
  s.loadFont(F7_11);
  int tw = spacedWidth(s, txt, 2);
  int w = 14 + 7 + 8 + tw + timeW + 14, h = s.height();
  int x = (s.width() - w) / 2;
  s.fillSmoothRoundRect(x, 0, w, h, h / 2, C_LINE, C_BG);
  s.fillSmoothRoundRect(x + 1, 1, w - 2, h - 2, h / 2 - 1, C_PANEL, C_LINE);
  s.fillSmoothCircle(x + 17, h / 2, 3, col, C_PANEL);
  s.setTextColor(col, C_PANEL);
  int tx = drawSpaced(s, txt, x + 29, h / 2, 2);
  if (tbuf[0]) {
    s.loadFont(F7_13);
    s.setTextColor(C_DIM, C_PANEL);
    s.drawString(tbuf, tx + 8, h / 2);
  }
  pushSpr(pillSpr, GX - pillSpr.width() / 2, GY + 96);  // in the ring's gap at the bottom
}

void drawStatePill() {
  if (showNoLink()) drawPill(tr(T_NO_LINK), C_RED);
  else drawPill(tr(stateText(state)), STATE_COLOR[state]);
  if (page == PAGE_HEATER) drawGaugePill();
}

// fan / output / time in state, in three cards
const Txt STAT_LABEL[3] = {T_STAT_FAN, T_STAT_OUT, T_STAT_TIME};
int statX(int i) { return 16 + i * 152; }

void drawStatCards() {
  tft.loadFont(F7_11);
  for (int i = 0; i < 3; i++) {
    tft.fillSmoothRoundRect(statX(i), STATS_Y, 144, 60, 12, C_PANEL, C_BG);
    tft.setTextColor(C_DIM, C_PANEL);
    drawSpaced(tft, tr(STAT_LABEL[i]), statX(i) + 12, STATS_Y + 17, 1);
  }
  setBodyFont(tft);
}

void drawStatValue(int i, const char *value, const char *unit) {
  TFT_eSprite &s = statSpr;
  s.fillSprite(C_PANEL);
  s.loadFont(F7_20);
  s.setTextDatum(L_BASELINE);
  s.setTextColor(C_TEXT, C_PANEL);
  s.drawString(value, 0, 22);
  if (unit) {
    int x = s.textWidth(value) + 2;
    s.loadFont(F7_13);
    s.setTextColor(C_DIM, C_PANEL);
    s.drawString(unit, x, 22);
  }
  pushSpr(s, statX(i) + 12, STATS_Y + 26);
}

void drawStats() {
  char buf[12];
  snprintf(buf, sizeof buf, "%d", (int)lroundf(fanRpm / 10) * 10);
  drawStatValue(0, buf, nullptr);
  snprintf(buf, sizeof buf, "%d", (int)lroundf(output * 100));
  drawStatValue(1, buf, "%");
  drawStatValue(2, fmtTime(millis() - stateSince, buf, sizeof buf), nullptr);
}

// the setpoint's limits, for greying out - and + at the ends
void setRange(int &lo, int &hi, int &v) {
  if (mode == MODE_TEMP) { lo = 1; hi = 30; v = setTemp; }
  else if (mode == MODE_POWER) { lo = 0; hi = 9; v = powerLevel; }
  else { lo = 1; hi = 9; v = fanLevel; }
}

void drawAdjButton(const Button &b, bool plus, bool pressed) {
  int lo, hi, v;
  setRange(lo, hi, v);
  bool enabled = plus ? v < hi : v > lo;
  uint16_t fill = pressed && enabled ? C_PRESS : C_RAISED;
  tft.fillSmoothRoundRect(b.x, b.y, b.w, b.h, 16, fill, C_BG);
  tft.drawFastHLine(b.x + 16, b.y + 1, b.w - 32, tft.alphaBlend(16, TFT_WHITE, fill));  // the app's inset highlight
  uint16_t col = enabled ? C_TEXT : tft.alphaBlend(90, C_TEXT, fill);
  int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
  tft.drawWideLine(cx - 11, cy, cx + 11, cy, 4, col, fill);
  if (plus) tft.drawWideLine(cx, cy - 11, cx, cy + 11, 4, col, fill);
}

// SETPOINT / POWER LEVEL / FAN SPEED in the mode's colour over the value
void drawSetValue() {
  char buf[8];
  const char *unit;
  Txt label;
  if (mode == MODE_TEMP) { snprintf(buf, sizeof buf, "%d", setTemp); unit = "C"; label = T_SETPOINT; }
  else if (mode == MODE_POWER) { snprintf(buf, sizeof buf, "%d", powerLevel); unit = "/ 9"; label = T_POWER_LEVEL; }
  else { snprintf(buf, sizeof buf, "%d", fanLevel); unit = "/ 9"; label = T_FAN_SPEED; }
  TFT_eSprite &s = setvalSpr;
  int w = s.width(), h = s.height();
  s.fillSprite(C_BG);
  s.fillSmoothRoundRect(0, 0, w, h, 16, C_PANEL, C_BG);
  s.loadFont(F7_11);
  s.setTextColor(MODE_COLOR[mode], C_PANEL);
  drawSpacedCentre(s, tr(label), w / 2, 17, 2);
  s.loadFont(F7_15);
  int uw = s.textWidth(unit) + (mode == MODE_TEMP ? 8 : 0);
  s.loadFont(F7_30);
  int vw = s.textWidth(buf);
  int x = (w - vw - 4 - uw) / 2;
  const int base = 56;
  s.setTextDatum(L_BASELINE);
  s.setTextColor(C_TEXT, C_PANEL);
  s.drawString(buf, x, base);
  s.loadFont(F7_15);
  s.setTextColor(C_DIM, C_PANEL);
  if (mode == MODE_TEMP) {
    drawDegree(s, x + vw + 7, base - 12, 2, C_DIM, C_PANEL);
    s.drawString(unit, x + vw + 12, base);
  } else {
    s.drawString(unit, x + vw + 5, base);
  }
  pushSpr(s, btnMinus.x + btnMinus.w + 8, btnMinus.y);
}

bool modeAllowed(int m) {
  bool burning = state == ST_STARTING || state == ST_RUNNING;
  return !((burning && m == MODE_VENT) || (state == ST_VENTING && m != MODE_VENT));
}

void drawModeSelector() {
  tft.fillSmoothRoundRect(SEG_X, SEG_Y, SEG_W, SEG_H, 14, C_PANEL, C_BG);
  tft.loadFont(F7_13);
  const int bw = (SEG_W - 16) / 3;
  for (int i = 0; i < 3; i++) {
    int x = SEG_X + 4 + i * (bw + 4);
    bool on = i == mode;
    uint16_t fill = on ? MODE_COLOR[i] : C_PANEL;
    if (on) tft.fillSmoothRoundRect(x, SEG_Y + 4, bw, SEG_H - 8, 10, fill, C_PANEL);
    uint16_t col = on ? TFT_WHITE : modeAllowed(i) ? C_DIM : tft.alphaBlend(77, C_DIM, C_PANEL);
    tft.setTextColor(col, fill);
    drawSpacedCentre(tft, tr(modeText(i)), x + bw / 2, SEG_Y + SEG_H / 2, 2);
  }
  setBodyFont(tft);
}

// app-style power button: a green or red pill across the page with the
// power symbol and what it does; grey while the heater cools down
void drawPowerButton() {
  const Button &b = btnPower;
  uint16_t fill = state == ST_COOLDOWN ? C_RAISED : state == ST_OFF ? C_GREEN : C_RED;
  uint16_t ink = state == ST_COOLDOWN ? C_DIM : TFT_WHITE;
  Txt t = state == ST_OFF ? (mode == MODE_VENT ? T_START_FAN : T_START) : state == ST_COOLDOWN ? T_STOPPING : T_STOP;
  tft.fillRect(b.x, b.y, b.w, b.h + 8, C_BG);
  if (state != ST_COOLDOWN)  // the app's soft glow under the button
    tft.fillSmoothRoundRect(b.x + 24, b.y + 10, b.w - 48, b.h, b.h / 2, tft.alphaBlend(60, fill, C_BG), C_BG);
  tft.fillSmoothRoundRect(b.x, b.y, b.w, b.h, b.h / 2, fill, C_BG);
  tft.loadFont(F7_17);
  int tw = tft.textWidth(tr(t));
  int x = b.x + (b.w - (24 + 12 + tw)) / 2, cy = b.y + b.h / 2;
  int px = x + 12;
  tft.drawSmoothArc(px, cy + 1, 11, 8, 0, 140, ink, fill, true);
  tft.drawSmoothArc(px, cy + 1, 11, 8, 220, 360, ink, fill, true);
  tft.drawWideLine(px, cy - 11, px, cy - 2, 3, ink, fill);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(ink, fill);
  tft.drawString(tr(t), x + 36, cy);
  setBodyFont(tft);
}

void drawStatic() {
  tft.fillScreen(C_BG);
  drawHeader(true);
  tft.loadFont(F7_11);
  tft.setTextColor(C_DIM, C_BG);
  drawSpacedCentre(tft, tr(T_MAKER), SCR_W / 2, HDR_H + 22, 3);
  setBodyFont(tft);
  drawGaugeScale();
  drawStatCards();
  drawAdjButton(btnMinus, false, false);
  drawAdjButton(btnPlus, true, false);
  drawModeSelector();
  drawPowerButton();
  drawStatePill();
}

// redraw the parts that depend on live values, only when they change
void drawDynamic(bool force = false) {
  static int lastTemp = INT32_MIN, lastSet = -1, lastRpm = -1, lastOut = -1, lastSec = -1;
  static Mode lastMode = MODE_TEMP;
  static HeaterState lastState = ST_OFF;
  static bool lastSensor = false;
  int t = lroundf(cabinTemp * 10);
  int s = mode == MODE_TEMP ? setTemp : mode == MODE_POWER ? powerLevel : fanLevel;
  int sec = (millis() - stateSince) / 1000;

  if (force || t != lastTemp || s != lastSet || mode != lastMode) {
    drawGauge();
    drawTemperature();
    drawSetValue();
  }
  if (force || s != lastSet || mode != lastMode || state != lastState) {
    drawAdjButton(btnMinus, false, false);
    drawAdjButton(btnPlus, true, false);
  }
  if (force || mode != lastMode || state != lastState) {
    drawModeSelector();
    drawPowerButton();
  }
  if (force || sensorOk != lastSensor) drawCabinLabel();
  if (force || (int)fanRpm / 10 != lastRpm || (int)(output * 100) != lastOut || sec != lastSec) drawStats();
  if (force || state != lastState || (state != ST_OFF && sec != lastSec)) drawGaugePill();
  drawVoltage();

  lastTemp = t; lastSet = s; lastMode = mode; lastState = state; lastSensor = sensorOk;
  lastRpm = (int)fanRpm / 10; lastOut = (int)(output * 100); lastSec = sec;
}

// over the temperature: a flickering flame while burning, a turning fan
// while it ventilates or cools down, a glow plug coil while it starts
void drawAnimation() {
  static bool blank = false;
  const int cx = anim.width() / 2;
  float phase = ((animTick * 37) % 10) / 9.0f;  // irregular flicker
  bool fan = state == ST_VENTING || state == ST_COOLDOWN;
  bool glow = state == ST_STARTING && millis() - stateSince <= START_MS / 2;
  bool flame = state == ST_RUNNING || (state == ST_STARTING && !glow);
  // with nothing to show, the space is already clear (a page draw clears it too)
  if (!fan && !glow && !flame && blank) return;
  blank = !fan && !glow && !flame;
  anim.fillSprite(C_BG);
  if (flame) drawFlame(anim, cx, 30, 10, phase, C_BG);
  else if (glow) for (int k = 0; k < 4; k++) anim.drawWideLine(cx - 6, 6 + k * 7, cx + 6, 9 + k * 7, 3, C_YELLOW, C_BG);
  else if (fan) drawFan(anim, cx, 18, 15, animTick * 0.5f, C_BLUE, C_BG);
  pushSpr(anim, GX - anim.width() / 2, GY - 86);
}

// ---------------------------------------------------------------- utilities page
const uint16_t TILE_COLOR[3] = {C_YELLOW, C_BLUE, C_ORANGE};

void drawTileIcon(int i, int cx, int cy, uint16_t col, uint16_t bg) {
  switch (i) {
    case 0:  // bulb
      tft.fillSmoothCircle(cx, cy - 4, 11, col, bg);
      tft.fillRect(cx - 6, cy + 3, 12, 6, col);
      tft.fillSmoothRoundRect(cx - 6, cy + 11, 12, 3, 1, col, bg);
      tft.fillSmoothRoundRect(cx - 4, cy + 15, 8, 3, 1, col, bg);
      break;
    case 1:  // drop
      tft.fillSmoothCircle(cx, cy + 5, 11, col, bg);
      fillSmoothTriangle(tft, cx - 10, cy + 1, cx + 10, cy + 1, cx, cy - 16, col, bg);
      break;
    case 2:  // pump: arrow down to a wave
      tft.drawWideLine(cx, cy - 15, cx, cy + 1, 3, col, bg);
      tft.drawWideLine(cx - 6, cy - 5, cx, cy + 1, 3, col, bg);
      tft.drawWideLine(cx + 6, cy - 5, cx, cy + 1, 3, col, bg);
      for (int k = 0; k < 4; k++) {
        int x = cx - 16 + k * 8;
        tft.drawWideLine(x, cy + 12, x + 4, cy + 8, 3, col, bg);
        tft.drawWideLine(x + 4, cy + 8, x + 8, cy + 12, 3, col, bg);
      }
      break;
  }
}

void drawToggle(int i) {
  const Button &b = btnToggle[i];
  const Toggle &t = toggles[i];
  uint16_t col = TILE_COLOR[i];
  uint16_t fill = t.on ? tft.alphaBlend(41, col, C_PANEL) : C_PANEL;
  tft.fillSmoothRoundRect(b.x, b.y, b.w, b.h, 16, t.on ? col : C_PANEL, C_BG);
  tft.fillSmoothRoundRect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, 15, fill, t.on ? col : C_PANEL);
  int cx = b.x + b.w / 2, iy = b.y + 36;
  if (t.on) tft.fillSmoothCircle(cx, iy, 24, tft.alphaBlend(50, col, fill), fill);  // glow
  drawTileIcon(i, cx, iy, t.on ? col : C_DIM, t.on ? tft.alphaBlend(50, col, fill) : fill);
  tft.loadFont(F7_13);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(C_TEXT, fill);
  tft.drawString(tr(t.name), cx, b.y + 80);
  tft.loadFont(F7_11);
  tft.setTextColor(t.on ? col : C_DIM, fill);
  drawSpacedCentre(tft, tr(t.on ? t.onText : t.offText), cx, b.y + 106, 2);
  setBodyFont(tft);
}

// UP / DOWN: hold to run. Greyed out on a remote, which can't run it.
void drawWinchButton(const Button &b, bool up, bool active) {
  uint16_t col = isRemote() ? C_TRACK : up ? C_GREEN : C_ORANGE;
  uint16_t fill = active ? (up ? C_GREEN : C_ORANGE) : C_RAISED;
  tft.fillSmoothRoundRect(b.x, b.y, b.w, b.h, 16, fill, C_PANEL);
  if (!active) tft.drawFastHLine(b.x + 16, b.y + 1, b.w - 32, tft.alphaBlend(16, TFT_WHITE, fill));
  int cx = b.x + b.w / 2, cy = b.y + b.h * 2 / 5;
  uint16_t ink = active ? C_BG : col;
  if (up) fillSmoothTriangle(tft, cx - 30, cy + 18, cx + 30, cy + 18, cx, cy - 24, ink, fill);
  else fillSmoothTriangle(tft, cx - 30, cy - 18, cx + 30, cy - 18, cx, cy + 24, ink, fill);
  tft.loadFont(F7_17);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(active ? C_BG : isRemote() ? C_DIM : C_TEXT, fill);
  tft.drawString(tr(up ? T_UP : T_DOWN), cx, b.y + b.h * 4 / 5);
  setBodyFont(tft);
}

// the card's state line, the chain counter and the bar under it
void drawWinchStatus() {
  const char *txt = tr(winch == WIN_UP ? T_HAULING : winch == WIN_DOWN ? T_LETTING_OUT
                       : chainOut <= 0 ? T_ANCHOR_HOME : T_STOPPED);
  uint16_t col = winch == WIN_UP ? C_GREEN : winch == WIN_DOWN ? C_ORANGE : C_DIM;
  tft.fillRect(WIN_X + WIN_W / 2, WIN_Y + 14, WIN_W / 2 - 16, 22, C_PANEL);
  tft.loadFont(F7_11);
  tft.setTextColor(col, C_PANEL);
  drawSpaced(tft, txt, WIN_X + WIN_W - 16 - spacedWidth(tft, txt, 2), WIN_Y + 25, 2);
  setBodyFont(tft);

  char buf[12];
  snprintf(buf, sizeof buf, "%.1f", chainOut);
  TFT_eSprite &s = chainSpr;
  s.fillSprite(C_PANEL);
  s.loadFont(F7_44);
  s.setTextDatum(L_BASELINE);
  s.setTextColor(C_TEXT, C_PANEL);
  s.drawString(buf, 0, 46);
  int x = s.textWidth(buf) + 8;
  s.loadFont(F7_15);
  s.setTextColor(C_DIM, C_PANEL);
  s.drawString(tr(T_CHAIN_OUT), x, 46);
  pushSpr(s, WIN_X + 16, WIN_Y + 44);

  // how much of the 60 m is out, peach -> orange like the app's bar
  const int bx = WIN_X + 16, by = WIN_Y + 110, bw = WIN_W - 32, bh = 10;
  int fw = max(bh, (int)(bw * constrain(chainOut / CHAIN_MAX, 0.0f, 1.0f)));
  tft.fillSmoothRoundRect(bx, by, bw, bh, bh / 2, C_TRACK, C_PANEL);
  if (chainOut > 0)
    tft.fillSmoothRoundRect(bx, by, fw, bh, bh / 2, tft.alphaBlend(fw * 255 / bw, C_ORANGE, C_PEACH), C_TRACK);
}

void drawUtilPill() {
  if (showNoLink()) drawPill(tr(T_NO_LINK), C_RED);
  else drawPill(tr(T_DEMO_RELAYS), C_ORANGE);
}

void drawUtilPage() {
  tft.fillScreen(C_BG);
  drawHeader(true);
  drawUtilPill();
  for (int i = 0; i < 3; i++) drawToggle(i);
  tft.setTextColor(C_DIM, C_BG);
  drawWrapped(tr(T_BILGE_NOTE), 18, btnToggle[0].y + btnToggle[0].h + 10, SCR_W - 36, 18);

  tft.fillSmoothRoundRect(WIN_X, WIN_Y, WIN_W, WIN_H, 16, C_PANEL, C_BG);
  tft.loadFont(F7_11);
  tft.setTextColor(C_DIM, C_PANEL);
  drawSpaced(tft, tr(T_WINDLASS), WIN_X + 16, WIN_Y + 25, 2);
  tft.loadFont(F7_13);
  tft.setTextDatum(TL_DATUM);
  const int sy = WIN_Y + 128;  // the scale under the bar
  tft.drawString("0 m", WIN_X + 16, sy);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("30", WIN_X + WIN_W / 2, sy);
  tft.setTextDatum(TR_DATUM);
  tft.drawString("60 m", WIN_X + WIN_W - 16, sy);
  setBodyFont(tft);
  drawWinchButton(btnWinUp, true, winch == WIN_UP);
  drawWinchButton(btnWinDown, false, winch == WIN_DOWN);
  if (isRemote()) {
    tft.setTextColor(C_DIM, C_PANEL);
    drawWrapped(tr(T_LOCK), WIN_X + 16, btnWinUp.y + btnWinUp.h + 14, WIN_W - 32, 18);
  }
  drawWinchStatus();
}

// ---------------------------------------------------------------- home page
// none on the 7": showPage() turns PAGE_HOME into the heater page
void drawHomePage() {}
void drawHomeStatus(bool force = false) {}
void drawHomeRolePill() {}
