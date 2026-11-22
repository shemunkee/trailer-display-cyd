#include "display.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

#include "board.h"
#include "touch.h"

#if defined(ST7789_DRIVER)
#define DRIVER_NAME "ST7789"
#elif defined(ILI9341_2_DRIVER)
#define DRIVER_NAME "ILI9341"
#else
#define DRIVER_NAME "unknown"
#endif

static TFT_eSPI tft;
static lv_display_t* disp = nullptr;

// One partial buffer, 20 lines (12.8 KB). No PSRAM, so keep it small.
static constexpr int BUF_LINES = 20;
// Allocated at start-up rather than static: the static RAM region is full
static constexpr size_t BUF_BYTES = SCREEN_W * BUF_LINES * 2;
static uint16_t* drawBuf = nullptr;

static constexpr uint32_t BL_PWM_HZ = 5000;
static constexpr uint8_t BL_PWM_BITS = 8;

static void flush(lv_display_t* d, const lv_area_t* area, uint8_t* px) {
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  // LVGL renders little-endian RGB565, the panel wants big-endian
  tft.pushColors(reinterpret_cast<uint16_t*>(px), w * h, true);
  tft.endWrite();
  lv_display_flush_ready(d);
}

static uint32_t tick() { return millis(); }

// Reads the ILI9341 ID register. A real ILI9341 answers 0x9341; anything else
// is most likely an ST7789. Only a hint for picking the right env.
static void logPanel() {
  uint16_t id = (tft.readcommand8(0xD3, 2) << 8) | tft.readcommand8(0xD3, 3);
  Serial.printf("Display: built for %s, panel ID read 0x%04X (%s)\n", DRIVER_NAME, id,
                id == 0x9341 ? "ILI9341" : "not ILI9341, likely ST7789");
}

void display_begin() {
  tft.init();
  tft.setRotation(1);  // landscape
#ifdef CYD_INVERT
  tft.invertDisplay(true);
#endif
  tft.fillScreen(TFT_BLACK);
  logPanel();

  // TFT_eSPI switched the backlight on as a plain GPIO; take it over with PWM
  ledcAttach(PIN_BACKLIGHT, BL_PWM_HZ, BL_PWM_BITS);
  display_set_brightness(100);
}

void display_lvgl_begin() {
  lv_init();
  lv_tick_set_cb(tick);
  disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_flush_cb(disp, flush);
  drawBuf = (uint16_t*)heap_caps_malloc(BUF_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  lv_display_set_buffers(disp, drawBuf, nullptr, BUF_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);
}

void display_set_brightness(uint8_t pct) {
  if (pct > 100) pct = 100;
  ledcWrite(PIN_BACKLIGHT, (uint32_t)pct * ((1 << BL_PWM_BITS) - 1) / 100);
}

void display_refresh() {
  if (!disp) return;
  lv_obj_invalidate(lv_display_get_screen_active(disp));
  lv_obj_invalidate(lv_display_get_layer_top(disp));
}

// Landscape both ways: rotation 1 has the USB on the right, 3 on the left
void display_set_flipped(bool f) {
  tft.setRotation(f ? 3 : 1);
  touch_set_flipped(f);
  display_refresh();
}

TFT_eSPI& display_tft() { return tft; }
