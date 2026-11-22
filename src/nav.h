// The main UI (SPEC.md 6.2): one content area above a 40px bottom bar with one
// icon button per screen. Screens are built once and shown or hidden, so going
// back to one (or waking from snooze) is instant. A 500 ms timer feeds them the
// latest values.
#pragma once

#include <lvgl.h>

enum class Page { Overview, Battery, Solar, Ac, Alarms, Settings, Count };

void nav_begin();  // builds every screen and shows Overview
void nav_show(Page p);
void nav_refresh();  // redraw the screens on the next LVGL pass instead of waiting for the timer
