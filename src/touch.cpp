#include "touch.h"

#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

#include "board.h"

static SPIClass touchSpi(VSPI);
static XPT2046_Touchscreen ts(XPT_CS, XPT_IRQ);

// screenX = m[0]*rx + m[1]*ry + m[2], screenY = m[3]*rx + m[4]*ry + m[5]
static float cal[6];
static bool calValid = false;
static bool flipped = false;

static int16_t lastX = 0, lastY = 0;
static bool lastPressed = false;

void touch_begin() {
  touchSpi.begin(XPT_CLK, XPT_MISO, XPT_MOSI, XPT_CS);
  ts.begin(touchSpi);
  ts.setRotation(0);  // raw axes; calibration handles rotation and mirroring
}

bool touch_raw(int16_t& x, int16_t& y) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  x = p.x;
  y = p.y;
  return true;
}

void touch_set_calibration(const float m[6]) {
  memcpy(cal, m, sizeof(cal));
  calValid = true;
}

void touch_set_flipped(bool f) { flipped = f; }
bool touch_flipped() { return flipped; }

bool touch_point(int16_t& x, int16_t& y) {
  int16_t rx, ry;
  if (!calValid || !touch_raw(rx, ry)) return false;
  float fx = cal[0] * rx + cal[1] * ry + cal[2];
  float fy = cal[3] * rx + cal[4] * ry + cal[5];
  if (flipped) {  // the calibration is for the normal orientation
    fx = SCREEN_W - 1 - fx;
    fy = SCREEN_H - 1 - fy;
  }
  x = constrain((int)lroundf(fx), 0, SCREEN_W - 1);
  y = constrain((int)lroundf(fy), 0, SCREEN_H - 1);
  return true;
}

static void readCb(lv_indev_t*, lv_indev_data_t* data) {
  int16_t x, y;
  lastPressed = touch_point(x, y);
  if (lastPressed) {
    lastX = x;
    lastY = y;
  }
  data->point.x = lastX;
  data->point.y = lastY;
  data->state = lastPressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void touch_lvgl_begin() {
  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, readCb);
}

bool touch_last(int16_t& x, int16_t& y) {
  x = lastX;
  y = lastY;
  return lastPressed;
}
