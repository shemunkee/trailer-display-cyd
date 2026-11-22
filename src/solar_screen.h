// Solar (SPEC.md 6.5): total at the top, Driver and Passenger cards below.
// MPPTs are matched by CustomName ("driver", "passenger", any case), never by
// instance number (SPEC 4). A card with no matching name takes the next charger
// that hasn't been placed, titled with its own name.
#pragma once

#include <lvgl.h>

#include "victron_data.h"

void solar_screen_create(lv_obj_t* page);
void solar_screen_update(const VictronData& d);
