// Battery (SPEC.md 6.4): SOC card on the left, rows on the right.
#pragma once

#include <lvgl.h>

#include "victron_data.h"

void battery_screen_create(lv_obj_t* page);
void battery_screen_update(const VictronData& d);
