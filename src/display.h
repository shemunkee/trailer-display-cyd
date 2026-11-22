// Display: TFT_eSPI panel, LVGL display driver and backlight.
#pragma once

#include <stdint.h>

class TFT_eSPI;

void display_begin();         // panel init, landscape, backlight on
void display_lvgl_begin();    // lv_init + LVGL display with a partial draw buffer
void display_set_brightness(uint8_t pct);
void display_set_flipped(bool f);  // 180 degrees (USB on the left); touch follows
void display_refresh();       // redraw the whole LVGL screen (after drawing on the panel directly)
TFT_eSPI& display_tft();      // direct panel access, for the calibration screen only
