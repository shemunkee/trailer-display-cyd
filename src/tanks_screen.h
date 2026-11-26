// Tanks (SPEC.md 6.12): one card per LPG tank (up to two, sorted by name), as in
// the Tanks mockup: name, level bar, big percent, pounds left, type and status.
#pragma once

#include <lvgl.h>

#include "victron_data.h"

static constexpr int TANKS_SHOWN = 2;
static constexpr float TANK_LOW_PCT = 20.0f;  // amber at or below

void tanks_screen_create(lv_obj_t* page);
void tanks_screen_update(const VictronData& d);

// "Driver Side Tank" -> "DRIVER SIDE" (trailing "Tank" dropped, uppercase)
void tank_title(const TankData& t, char* buf, size_t len);
