// Settings (SPEC.md 6.8): Low SOC, Snooze, Brightness, and Data source. Tapping
// Data source opens a small menu with Run setup and Recalibrate touch.
#pragma once

#include <lvgl.h>

void settings_screen_create(lv_obj_t* page);
void settings_screen_update();  // refreshes the Data source status
