// Overview (SPEC.md 6.3): AC input and Solar tiles on the left, SOC ring in the
// middle, AC loads and Quattro tiles on the right.
#pragma once

#include <lvgl.h>

#include "victron_data.h"

void overview_screen_create(lv_obj_t* page);
void overview_screen_update(const VictronData& d);
