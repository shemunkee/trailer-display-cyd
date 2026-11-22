#include "calibration.h"

#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <math.h>

#include "board.h"
#include "display.h"
#include "theme.h"
#include "touch.h"

static constexpr const char* NVS_NS = "touch";
static constexpr uint8_t CAL_VERSION = 1;
static constexpr float MAX_ERROR_PX = 8.0f;
static constexpr uint32_t BOOT_HOLD_MS = 3000;

struct Target {
  int16_t x, y;
};

// First three fit the matrix, the fourth checks it
static const Target targets[4] = {{30, 30}, {290, 120}, {160, 210}, {30, 210}};

static uint16_t c565(uint32_t hex) {
  return display_tft().color565((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
}

static void message(const char* line1, const char* line2 = nullptr) {
  TFT_eSPI& tft = display_tft();
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(c565(theme::TEXT), TFT_BLACK);
  tft.drawString(line1, SCREEN_W / 2, 92, 2);
  if (line2) {
    tft.setTextColor(c565(theme::MUTED), TFT_BLACK);
    tft.drawString(line2, SCREEN_W / 2, 112, 2);
  }
}

static void drawTarget(const Target& t) {
  TFT_eSPI& tft = display_tft();
  uint16_t c = c565(theme::AC);
  tft.drawFastHLine(t.x - 10, t.y, 21, c);
  tft.drawFastVLine(t.x, t.y - 10, 21, c);
  tft.drawCircle(t.x, t.y, 5, c);
}

static void waitRelease() {
  uint32_t seen = millis();
  int16_t x, y;
  while (millis() - seen < 150) {
    if (touch_raw(x, y)) seen = millis();
    delay(10);
  }
}

// Wait for a press, average the samples while it's held, wait for release
static bool sample(int16_t& rx, int16_t& ry) {
  int16_t x, y;
  while (!touch_raw(x, y)) delay(10);
  delay(50);  // let the reading settle
  int32_t sx = 0, sy = 0;
  int n = 0;
  while (n < 16 && touch_raw(x, y)) {
    sx += x;
    sy += y;
    n++;
    delay(10);
  }
  waitRelease();
  if (n < 6) return false;  // too short a tap to trust
  rx = sx / n;
  ry = sy / n;
  return true;
}

// Solve screen = a*rx + b*ry + c for X and Y from three points (Cramer's rule)
static bool solve(const Target targets[], const int16_t rx[], const int16_t ry[], float m[6]) {
  float x1 = rx[0], x2 = rx[1], x3 = rx[2];
  float y1 = ry[0], y2 = ry[1], y3 = ry[2];
  float det = x1 * (y2 - y3) + x2 * (y3 - y1) + x3 * (y1 - y2);
  if (fabsf(det) < 1.0f) return false;  // points nearly in a line
  for (int axis = 0; axis < 2; axis++) {
    float s1 = axis ? targets[0].y : targets[0].x;
    float s2 = axis ? targets[1].y : targets[1].x;
    float s3 = axis ? targets[2].y : targets[2].x;
    m[axis * 3 + 0] = (s1 * (y2 - y3) + s2 * (y3 - y1) + s3 * (y1 - y2)) / det;
    m[axis * 3 + 1] = (x1 * (s2 - s3) + x2 * (s3 - s1) + x3 * (s1 - s2)) / det;
    m[axis * 3 + 2] = (x1 * (y2 * s3 - y3 * s2) + x2 * (y3 * s1 - y1 * s3) + x3 * (y1 * s2 - y2 * s1)) / det;
  }
  return true;
}

static bool valid(const float m[6]) {
  for (int i = 0; i < 6; i++)
    if (!isfinite(m[i])) return false;
  return m[0] != 0 || m[1] != 0;
}

static void save(const float m[6]) {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.putBytes("m", m, sizeof(float) * 6);
  prefs.putUChar("v", CAL_VERSION);
  prefs.end();
}

bool calibration_load() {
  Preferences prefs;
  float m[6];
  prefs.begin(NVS_NS, false);
  bool ok = prefs.getUChar("v", 0) == CAL_VERSION && prefs.getBytes("m", m, sizeof(m)) == sizeof(m);
  prefs.end();
  if (!ok || !valid(m)) {
    Serial.println("Touch: no saved calibration");
    return false;
  }
  touch_set_calibration(m);
  Serial.println("Touch: calibration loaded");
  return true;
}

void calibration_run() {
  TFT_eSPI& tft = display_tft();
  Serial.println("Touch: calibrating");
  waitRelease();
  for (;;) {
    int16_t rx[4], ry[4];
    bool ok = true;
    for (int i = 0; i < 4 && ok; i++) {
      tft.fillScreen(TFT_BLACK);
      char step[24];
      snprintf(step, sizeof(step), "Point %d of 4", i + 1);
      message("Tap the center of the cross", step);
      drawTarget(targets[i]);
      ok = sample(rx[i], ry[i]);
    }

    // Saved for the normal orientation, so flipping later needs no recalibration:
    // a point drawn at (x, y) while flipped is (W-1-x, H-1-y) the normal way up
    Target normal[4];
    for (int i = 0; i < 4; i++) {
      normal[i] = targets[i];
      if (touch_flipped()) normal[i] = {(int16_t)(SCREEN_W - 1 - targets[i].x), (int16_t)(SCREEN_H - 1 - targets[i].y)};
    }
    float m[6];
    if (ok && solve(normal, rx, ry, m)) {
      float ex = m[0] * rx[3] + m[1] * ry[3] + m[2] - normal[3].x;
      float ey = m[3] * rx[3] + m[4] * ry[3] + m[5] - normal[3].y;
      float err = sqrtf(ex * ex + ey * ey);
      Serial.printf("Touch: check point off by %.1f px\n", err);
      if (err <= MAX_ERROR_PX) {
        save(m);
        touch_set_calibration(m);
        tft.fillScreen(TFT_BLACK);
        message("Calibration saved");
        delay(800);
        tft.fillScreen(TFT_BLACK);
        return;
      }
    }
    tft.fillScreen(TFT_BLACK);
    message("That didn't line up", "Let's try again");
    delay(1500);
  }
}

bool calibration_boot_hold() {
  int16_t x, y;
  if (!touch_raw(x, y)) return false;

  TFT_eSPI& tft = display_tft();
  tft.fillScreen(TFT_BLACK);
  message("Keep holding to recalibrate touch");
  uint32_t start = millis(), seen = millis();
  while (millis() - start < BOOT_HOLD_MS) {
    if (touch_raw(x, y)) seen = millis();
    else if (millis() - seen > 200) {  // released (short gaps are just noise)
      tft.fillScreen(TFT_BLACK);
      return false;
    }
    delay(20);
  }
  tft.fillScreen(TFT_BLACK);
  message("Release to start");
  waitRelease();
  return true;
}
