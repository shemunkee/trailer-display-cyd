// Setup portal (SPEC.md 6.9): the Victron_CYD WiFi network, captive DNS and the
// setup web page. Saving stores to NVS and restarts.
#pragma once

void setup_portal_begin(bool canCancel);  // after display_lvgl_begin
void setup_portal_loop();
