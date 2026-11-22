// XPT2046 resistive touch: raw reads, calibrated reads, and the LVGL input device.
#pragma once

#include <stdint.h>

void touch_begin();
bool touch_raw(int16_t& x, int16_t& y);    // true while pressed; raw controller units
void touch_set_calibration(const float m[6]);  // always for the normal (USB right) orientation
void touch_set_flipped(bool f);                // screen turned 180 degrees
bool touch_flipped();
bool touch_point(int16_t& x, int16_t& y);  // true while pressed; screen pixels (needs calibration)
void touch_lvgl_begin();                   // register as an LVGL pointer (after display_lvgl_begin)
bool touch_last(int16_t& x, int16_t& y);   // last point LVGL saw, and whether it was pressed
