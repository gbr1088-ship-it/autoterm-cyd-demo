// Draws the Lomax dashboard's screens on a PC, with the firmware's own
// drawing code (src/lomax_draw.h) and TFT_eSPI's sprites. See build.sh.
#include <TFT_eSPI.h>

HardwareSerial Serial;
SPIClass SPI;
TFT_eSPI tft;

static uint16_t fb[480][800];  // the top-left SCR_W x SCR_H of it is the screen

void screenPush(TFT_eSprite &s, int sx, int sy, int w, int h, int x, int y) {
  const uint16_t *p = (const uint16_t *)s.getPointer();
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++)
      if (y + r >= 0 && y + r < 480 && x + c >= 0 && x + c < 800) {
        uint16_t v = p[(sy + r) * s.width() + sx + c];
        fb[y + r][x + c] = (v >> 8) | (v << 8);  // sprites keep their pixels byte-swapped
      }
}

void screenFill(int x, int y, int w, int h, uint16_t col) {
  for (int r = y; r < y + h && r < 480; r++)
    for (int c = x; c < x + w && c < 800; c++) fb[r][c] = col;
}

#include "lomax_draw.h"

#if defined(BOARD_ROUND)
#define PREFIX "round_"
#elif defined(BOARD_7INCH)
#define PREFIX "7inch_"
#else
#define PREFIX ""
#endif

// name_<w>x<h>.rgb565, which build.sh turns into a PNG
static void save(const char *name) {
  char fn[64];
  snprintf(fn, sizeof fn, PREFIX "%s_%dx%d.rgb565", name, SCR_W, SCR_H);
  FILE *f = fopen(fn, "wb");
  for (int y = 0; y < SCR_H; y++) fwrite(fb[y], 2, SCR_W, f);
  fclose(f);
  fprintf(stderr, "wrote %s\n", fn);
}

int main() {
  drawSplash();
  save("splash");

  DashState st;
  st.mph = 48;
  st.gpsOk = true;
  st.rpm = 3650;
  st.indL = true;
  st.side = true;
  st.timeOk = true;
  st.hh = 14;
  st.mm = 37;
  st.ss = 22;
  st.odo = 12873.4f;
  st.trip = 86.7f;
  st.oilPsi = 42;
  st.oilTemp = 96;
  st.chtL = 182;
  st.chtR = 176;
  for (int p = 0; p < PAGE_COUNT; p++) {
    if (!hasPage(p)) continue;
    showPage((Page)p, st);
    updateDash(st, 0, true);
    char n[32];
    snprintf(n, sizeof n, "page%d", p);
    save(n);
  }
  // no GPS yet: needle at rest, warning on, clock stopped at ten past ten
  DashState cold;
  cold.beam = true;
  cold.indR = true;
  cold.odo = 12873.4f;
  showPage(nextPage((Page)(PAGE_COUNT - 1)), cold);
  updateDash(cold, 0, true);
  save("nogps");

  // the engine box: hazards on, oil pressure gone, a hot head; then no box
  DashState hot = st;
  hot.indL = hot.indR = hot.hazard = true;
  hot.oilPsi = 4;
  hot.oilLow = true;
  hot.oilTemp = 128;
  hot.chtL = 262;
  showPage(nextPage((Page)(PAGE_COUNT - 1)), hot);
  updateDash(hot, 0, true);
  save("warnings");
  DashState nobox = st;
  nobox.oilPsi = nobox.oilTemp = nobox.chtL = nobox.chtR = NAN;
  showPage(nextPage((Page)(PAGE_COUNT - 1)), nobox);
  updateDash(nobox, 0, true);
  save("nobox");
  return 0;
}
