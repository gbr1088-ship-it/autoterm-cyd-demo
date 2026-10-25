// 7 inch ESP32-S3 board: Waveshare ESP32-S3-Touch-LCD-7 (800x480 RGB panel,
// GT911 touch, CH422G IO expander for the resets and the backlight).
//
// The whole user interface is written for the CYD's 320x240 screen. Here `tft`
// is a 320x240 sprite in memory instead of a real display, so every drawing
// call in main.cpp works unchanged. A background task copies whatever changed
// onto the 800x480 panel at double size (640x480, centred), and widens the
// left and right edge pixels of each row into the 80 px side margins so
// headers and backgrounds run the full width.
//
// Touch comes from the GT911 over I2C and is scaled back to 320x240.
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
#define EXIO_USB_SEL 0x20  // high = USB on the USB port (low gives it to CAN)

// panel timing for the board's 7" 800x480 panel
#ifndef P7_PCLK_HZ
#define P7_PCLK_HZ 16000000  // lower it (e.g. 12000000) if the picture flickers or drifts
#endif

const int P7_W = 800, P7_H = 480;
const int UI_W = 320, UI_H = 240;          // what main.cpp draws
const int UI_SCALE = 2;
const int UI_OFF_X = (P7_W - UI_W * UI_SCALE) / 2;  // 80 px margins left and right

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

  // copy the changed area to the panel at double size
  void flush(esp_lcd_panel_handle_t panel) {
    portENTER_CRITICAL(&lock);
    int32_t x0 = dx0, y0 = dy0, x1 = dx1, y1 = dy1;
    dx0 = 1; dx1 = 0;  // empty
    portEXIT_CRITICAL(&lock);
    if (x0 > x1 || !created()) return;

    // an edge pixel that changed repaints that row's margin too
    int ox0 = x0 == 0 ? 0 : UI_OFF_X + x0 * UI_SCALE;
    int ox1 = x1 == UI_W - 1 ? P7_W - 1 : UI_OFF_X + x1 * UI_SCALE + UI_SCALE - 1;
    int ow = ox1 - ox0 + 1;
    const uint16_t *img = (const uint16_t *)getPointer();
    for (int y = y0; y <= y1; y += ROWS) {
      int rows = min(ROWS, (int)(y1 - y + 1));
      for (int r = 0; r < rows; r++) {
        const uint16_t *src = img + (y + r) * UI_W;
        uint16_t *dst = buf + r * UI_SCALE * P7_W;
        for (int ox = ox0; ox <= ox1; ox++) {
          int sx = (ox - UI_OFF_X) / UI_SCALE;
          if (ox < UI_OFF_X) sx = 0;
          if (sx > UI_W - 1) sx = UI_W - 1;
          uint16_t c = src[sx];
          dst[ox - ox0] = (c >> 8) | (c << 8);  // sprites keep their colours byte-swapped
        }
        memcpy(dst + P7_W, dst, ow * 2);  // second line of the doubled row
      }
      // the buffer rows are P7_W apart, so send each output row on its own
      for (int r = 0; r < rows * UI_SCALE; r++)
        esp_lcd_panel_draw_bitmap(panel, ox0, (y * UI_SCALE) + r, ox1 + 1, (y * UI_SCALE) + r + 1, buf + r * P7_W);
    }
  }

 private:
  static const int ROWS = 8;  // source rows converted per pass
  uint16_t buf[ROWS * UI_SCALE * P7_W];
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
uint8_t exio = EXIO_LCD_RST | EXIO_SD_CS | EXIO_USB_SEL;  // backlight off, touch held in reset

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

// Finger position in the 320x240 coordinates main.cpp uses. The GT911 only
// has a new reading every ~10 ms, so in between the last one is repeated.
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
        int px = p[0] | (p[1] << 8), py = p[2] | (p[3] << 8);
        lx = constrain((px - UI_OFF_X) / UI_SCALE, 0, UI_W - 1);
        ly = constrain(py / UI_SCALE, 0, UI_H - 1);
        down = true;
        Serial.printf("touch raw %d,%d -> %d,%d\n", px, py, lx, ly);
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

  touch7Begin();
}
