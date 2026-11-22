// Touch calibration: 3-point affine fit plus a 4th point to check it, saved in NVS.
#pragma once

bool calibration_load();       // load from NVS into touch; false if missing or invalid
void calibration_run();        // blocking; draws on the panel directly, saves and applies.
                               // If LVGL is running, call display_refresh() afterwards.
bool calibration_boot_hold();  // true if the screen is held for 3 s at power-up
