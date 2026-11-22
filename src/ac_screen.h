// AC / Quattro (SPEC.md 6.6): AC input and AC out cards, Quattro state bar below.
#pragma once

#include <lvgl.h>

#include "victron_data.h"

void ac_screen_create(lv_obj_t* page);
void ac_screen_update(const VictronData& d);
