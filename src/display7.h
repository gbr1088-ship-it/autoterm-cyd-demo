// 7 inch ESP32-S3 board: Waveshare ESP32-S3-Touch-LCD-7 (800x480 RGB panel,
// GT911 touch, CH422G IO expander for the resets and the backlight).
//
// The screen is mounted upright (portrait, 480x800). Here `tft` is a 480x800
// sprite in PSRAM instead of a real display, so every TFT_eSPI drawing call
// in main.cpp works unchanged; main.cpp lays the pages out for it (see SCR_W,
// U() and ui7.h). A background task copies whatever changed onto the 800x480
// panel, turning it a quarter turn and correcting the colours on the way (the
// panel shows mid-tones too light, so the orange looked washed out).
//
// Touch comes from the GT911 over I2C and is turned the same way.
// The backlight is only on or off: this board switches it through the CH422G
// rather than a PWM pin, so the brightness setting can't dim it.

#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>

// ---------------------------------------------------------------- pins
#define P7_DE     5
#define P7_VSYNC  3
#define P7_HSYNC  46
#define P7_PCLK   7
#define P7_I2C_SDA 8   // touch and IO expander share this bus
#define P7_I2C_SCL 9
#define P7_TOUCH_INT 4
// data lines in the order the LCD peripheral wants them: B0-B4, G0-G5, R0-R4
// (the panel's B3-B7, G2-G7 and R3-R7)
const int P7_DATA[16] = {14, 38, 18, 17, 10,  39, 0, 45, 48, 47, 21,  1, 2, 42, 41, 40};

// CH422G IO expander: it has no register map, each I2C address is a command
#define CH422G_MODE 0x24   // write 0x01: EXIO pins are outputs
#define CH422G_OUT  0x38   // write the EXIO0-7 levels
#define EXIO_TP_RST  0x02
#define EXIO_DISP    0x04  // backlight on
#define EXIO_LCD_RST 0x08
#define EXIO_SD_CS   0x10  // high = SD card not selected
#define EXIO_USB_SEL 0x20  // high gives the USB lines to the CAN chip; low keeps them on the USB socket

// Panel timing for the board's 7" 800x480 panel (Waveshare's values). The
// panel reads its frame buffer straight from PSRAM (no bounce buffers in
// IDF 4.4), so it can jitter while the CPU is busy in PSRAM too. 12 MHz
// left this panel white, so it stays at 16.
#ifndef P7_PCLK_HZ
#define P7_PCLK_HZ 16000000
#endif

const int P7_W = 800, P7_H = 480;   // the panel
const int UI_W = 480, UI_H = 800;   // what main.cpp draws

// Which way the board is turned: 1 = a quarter turn clockwise from landscape
// (the panel's left edge at the top, USB sockets at the top), 2 =
// anticlockwise (USB sockets at the bottom, as mounted in the van).
#ifndef P7_PORTRAIT
#define P7_PORTRAIT 2
#endif

// Colour correction on the way to the panel: each channel goes through
// v^P7_GAMMA. 1.0 leaves the colours alone; higher makes mid-tones darker
// and richer (the orange's green 145 and blue 76 come down) while black and
// full brightness stay put.
#ifndef P7_GAMMA
#define P7_GAMMA 1.5
#endif
uint8_t gammaR[32], gammaG[64], gammaB[32];

void makeGammaLuts() {
  for (int i = 0; i < 64; i++) {
    if (i < 32) {
      gammaR[i] = lroundf(powf(i / 31.0f, P7_GAMMA) * 31);
      gammaB[i] = gammaR[i];
    }
    gammaG[i] = lroundf(powf(i / 63.0f, P7_GAMMA) * 63);
  }
}

// sprite pixel (colours kept byte-swapped) -> corrected panel pixel
inline uint16_t panelColour(uint16_t s) {
  uint16_t c = (s >> 8) | (s << 8);
  return (gammaR[c >> 11] << 11) | (gammaG[(c >> 5) & 0x3F] << 5) | gammaB[c & 0x1F];
}

// ---------------------------------------------------------------- screen
// A full-screen sprite that remembers which part of it has been drawn on
// since the last copy to the panel. Every TFT_eSPI drawing call ends up in
// one of the virtual functions below (or pushImage), so tracking them is enough.
class ScreenSprite : public TFT_eSprite {
 public:
  explicit ScreenSprite(TFT_eSPI *parent) : TFT_eSprite(parent) {}

  void drawPixel(int32_t x, int32_t y, uint32_t c) override { TFT_eSprite::drawPixel(x, y, c); mark(x, y, 1, 1); }
  void drawLine(int32_t xs, int32_t ys, int32_t xe, int32_t ye, uint32_t c) override {
    TFT_eSprite::drawLine(xs, ys, xe, ye, c);
    mark(min(xs, xe), min(ys, ye), abs(xe - xs) + 1, abs(ye - ys) + 1);
  }
  void drawFastVLine(int32_t x, int32_t y, int32_t h, uint32_t c) override { TFT_eSprite::drawFastVLine(x, y, h, c); mark(x, y, 1, h); }
  void drawFastHLine(int32_t x, int32_t y, int32_t w, uint32_t c) override { TFT_eSprite::drawFastHLine(x, y, w, c); mark(x, y, w, 1); }
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c) override { TFT_eSprite::fillRect(x, y, w, h, c); mark(x, y, w, h); }
  // TFT_eSPI's own fillScreen uses the (unused) parent display's size
  void fillScreen(uint32_t c) { fillRect(0, 0, UI_W, UI_H, c); }
  void setWindow(int32_t xs, int32_t ys, int32_t xe, int32_t ye) override {
    TFT_eSprite::setWindow(xs, ys, xe, ye);
    mark(xs, ys, xe - xs + 1, ye - ys + 1);
  }
  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *data) {
    TFT_eSprite::pushImage(x, y, w, h, data);
    mark(x, y, w, h);
  }
  void pushImage(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t *data) {
    TFT_eSprite::pushImage(x, y, w, h, data);
    mark(x, y, w, h);
  }

  void mark(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (w <= 0 || h <= 0) return;
    int32_t x1 = min<int32_t>(x + w - 1, UI_W - 1), y1 = min<int32_t>(y + h - 1, UI_H - 1);
    x = max<int32_t>(x, 0); y = max<int32_t>(y, 0);
    if (x > x1 || y > y1) return;
    portENTER_CRITICAL(&lock);
    if (dx0 > dx1) { dx0 = x; dy0 = y; dx1 = x1; dy1 = y1; }
    else { dx0 = min(dx0, x); dy0 = min(dy0, y); dx1 = max(dx1, x1); dy1 = max(dy1, y1); }
    portEXIT_CRITICAL(&lock);
  }

  // Copy the changed area to the panel, turned a quarter turn: a few screen
  // columns at a time, each becoming a panel row.
  //   P7_PORTRAIT 1: panel x = screen y,           panel y = 479 - screen x
  //   P7_PORTRAIT 2: panel x = 799 - screen y,     panel y = screen x
  void flush(esp_lcd_panel_handle_t panel) {
    portENTER_CRITICAL(&lock);
    int32_t x0 = dx0, y0 = dy0, x1 = dx1, y1 = dy1;
    dx0 = 1; dx1 = 0;  // empty
    portEXIT_CRITICAL(&lock);
    if (x0 > x1 || !created()) return;

    int w = y1 - y0 + 1;  // panel row length
    const uint16_t *img = (const uint16_t *)getPointer();
    for (int a = x0; a <= x1; a += ROWS) {
      int b = min(a + ROWS - 1, (int)x1);
      for (int y = y0; y <= y1; y++) {  // screen rows in order, which suits the PSRAM cache
        const uint16_t *src = img + y * UI_W;
        for (int x = a; x <= b; x++) {
#if P7_PORTRAIT == 1
          buf[(b - x) * w + (y - y0)] = panelColour(src[x]);
#else
          buf[(x - a) * w + (y1 - y)] = panelColour(src[x]);
#endif
        }
      }
#if P7_PORTRAIT == 1
      esp_lcd_panel_draw_bitmap(panel, y0, P7_H - 1 - b, y1 + 1, P7_H - a, buf);
#else
      esp_lcd_panel_draw_bitmap(panel, P7_W - 1 - y1, a, P7_W - y0, b + 1, buf);
#endif
    }
  }

 private:
  static const int ROWS = 16;  // screen columns (panel rows) converted per pass
  uint16_t buf[ROWS * P7_W];
  portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
  int32_t dx0 = 0, dy0 = 0, dx1 = UI_W - 1, dy1 = UI_H - 1;  // everything, at the start
};

TFT_eSPI tftUnused;  // TFT_eSPI wants a parent display for its sprites; never started
ScreenSprite tft(&tftUnused);
esp_lcd_panel_handle_t panel7 = nullptr;

void panelFlushTask(void *) {
  for (;;) {
    tft.flush(panel7);
    vTaskDelay(pdMS_TO_TICKS(16));
  }
}

// ---------------------------------------------------------------- IO expander
// backlight off, touch held in reset, USB on the USB socket (for flashing and the log)
uint8_t exio = EXIO_LCD_RST | EXIO_SD_CS;

void exioWrite(uint8_t v) {
  exio = v;
  Wire.beginTransmission(CH422G_OUT);
  Wire.write(v);
  Wire.endTransmission();
}

void display7Backlight(uint8_t level) {
  exioWrite(level ? (exio | EXIO_DISP) : (exio & ~EXIO_DISP));
}

// ---------------------------------------------------------------- touch (GT911)
uint8_t gtAddr = 0x5D;
bool gtFound = false;

bool gtRead(uint16_t reg, uint8_t *data, size_t n) {
  Wire.beginTransmission(gtAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(gtAddr, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) data[i] = Wire.read();
  return true;
}

void gtWrite(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(gtAddr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(v);
  Wire.endTransmission();
}

void touch7Begin() {
  // reset the GT911 with its INT pin low, which puts it on address 0x5D
  pinMode(P7_TOUCH_INT, OUTPUT);
  digitalWrite(P7_TOUCH_INT, LOW);
  exioWrite(exio & ~EXIO_TP_RST);
  delay(100);
  exioWrite(exio | EXIO_TP_RST);
  delay(200);
  pinMode(P7_TOUCH_INT, INPUT);
  // in case it came up on its other address anyway
  for (uint8_t a : {0x5D, 0x14}) {
    gtAddr = a;
    uint8_t id[4];
    if (gtRead(0x8140, id, 4)) {
      gtFound = true;
      Serial.printf("[touch] GT911 at 0x%02X, id %c%c%c%c\n", a, id[0], id[1], id[2], id[3]);
      return;
    }
  }
  Serial.println("[touch] no GT911 found");
}

// Finger position in screen pixels. The GT911 only has a new reading every
// ~10 ms, so in between the last one is repeated.
bool touch7Read(int &x, int &y) {
  static bool down = false;
  static int lx = 0, ly = 0;
  static uint32_t lastPoll = 0;
  if (!gtFound) return false;
  if (millis() - lastPoll < 10) { x = lx; y = ly; return down; }
  lastPoll = millis();
  uint8_t st;
  if (!gtRead(0x814E, &st, 1)) return down = false;
  if (st & 0x80) {  // a fresh reading
    down = false;
    if ((st & 0x0F) > 0) {
      uint8_t p[4];
      if (gtRead(0x8150, p, 4)) {
        int px = constrain(p[0] | (p[1] << 8), 0, P7_W - 1), py = constrain(p[2] | (p[3] << 8), 0, P7_H - 1);
#if P7_PORTRAIT == 1
        lx = P7_H - 1 - py;
        ly = px;
#else
        lx = py;
        ly = P7_W - 1 - px;
#endif
        down = true;
        Serial.printf("touch %d,%d\n", lx, ly);
      }
    }
    gtWrite(0x814E, 0);
  }
  x = lx; y = ly;
  return down;
}

// ---------------------------------------------------------------- start-up
void display7Begin() {
  Wire.begin(P7_I2C_SDA, P7_I2C_SCL, 400000);
  Wire.beginTransmission(CH422G_MODE);
  Wire.write(0x01);
  Wire.endTransmission();
  exioWrite(exio);  // backlight off until the splash fades in; panel out of reset

  // TFT_eSPI opens an SPI transaction around sprite drawing too, so its bus
  // has to exist. It drives nothing: the default pins are the SD card slot's.
  SPI.begin();

  makeGammaLuts();
  tft.setColorDepth(16);
  if (!tft.createSprite(UI_W, UI_H)) Serial.println("[display] no memory for the screen sprite");

  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_PLL160M;
  cfg.timings.pclk_hz = P7_PCLK_HZ;
  cfg.timings.h_res = P7_W;
  cfg.timings.v_res = P7_H;
  cfg.timings.hsync_pulse_width = 4;  // Waveshare's own example values
  cfg.timings.hsync_back_porch = 8;
  cfg.timings.hsync_front_porch = 8;
  cfg.timings.vsync_pulse_width = 4;
  cfg.timings.vsync_back_porch = 8;
  cfg.timings.vsync_front_porch = 8;
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.data_width = 16;
  cfg.psram_trans_align = 64;
  cfg.hsync_gpio_num = P7_HSYNC;
  cfg.vsync_gpio_num = P7_VSYNC;
  cfg.de_gpio_num = P7_DE;
  cfg.pclk_gpio_num = P7_PCLK;
  for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = P7_DATA[i];
  cfg.disp_gpio_num = -1;
  cfg.flags.fb_in_psram = 1;
  if (esp_lcd_new_rgb_panel(&cfg, &panel7) != ESP_OK) {
    Serial.println("[display] RGB panel didn't start (is PSRAM enabled?)");
    return;
  }
  esp_lcd_panel_reset(panel7);
  esp_lcd_panel_init(panel7);
  // WiFi runs on core 0 too, at a higher priority, so drawing never holds it up
  xTaskCreatePinnedToCore(panelFlushTask, "panel", 4096, nullptr, 1, nullptr, 0);
  Serial.printf("[display] %dx%d panel started, portrait %d, gamma %.2f\n", P7_W, P7_H, P7_PORTRAIT, (float)P7_GAMMA);

  touch7Begin();
}

// Screenshots for checking the layout from a computer (main.cpp sends one
// when an 'S' arrives over USB): "SHOT 480 800\n" followed by the screen's
// pixels, RGB565 big-endian, row by row (tools/screenshot7.py saves a PNG).
void display7Screenshot() {
  if (!tft.created()) return;
  Serial.printf("SHOT %d %d\n", UI_W, UI_H);
  Serial.flush();
  // byte-swapped = big-endian; in pieces, as the USB port drops what doesn't fit its buffer
  const uint8_t *p = (const uint8_t *)tft.getPointer();
  size_t left = UI_W * UI_H * 2;
  uint32_t start = millis();
  while (left && millis() - start < 10000) {
    size_t n = Serial.write(p, min(left, (size_t)4096));
    p += n;
    left -= n;
    if (!n) delay(1);
  }
  Serial.flush();
}
