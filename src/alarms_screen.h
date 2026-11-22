// Alarms (SPEC.md 6.7): active device alarms and warnings, plus our own low SOC
// warning (6.11). Two cards at a time; with more, a "tap for more" line pages
// through them (no swipes on resistive touch).
#pragma once

#include <lvgl.h>

#include "victron_data.h"

void alarms_screen_create(lv_obj_t* page);
int alarms_screen_update(const VictronData& d);  // returns how many are active, for the nav dot
