// CYD Victron display (see SPEC.md)
// Built so far: display, touch with saved calibration, settings in NVS, setup portal, local and cloud MQTT,
// snooze, Overview and Settings screens with the bottom nav.

#include <Arduino.h>
#include <lvgl.h>

#include "calibration.h"
#include "display.h"
#include "mqtt_link.h"
#include "nav.h"
#include "net_config.h"
#include "ota.h"
#include "settings.h"
#include "setup_portal.h"
#include "snooze.h"
#include "touch.h"

static bool setupMode = false;

void setup() {
  Serial.begin(115200);
  Serial.println("\nCYD Victron display starting");

  ota_begin();
  settings_load();
  NetConfig net;
  net_config_load(net);
  net_config_log(net);
  // Setup opens when nothing is saved yet, or when "Run setup" asked for it
  setupMode = net_config_take_setup_request() || !net_config_has_network(net);

  display_begin();
  display_set_brightness(settings_get().brightnessPct);
  display_set_flipped(settings_get().flipped);  // before calibration or anything else draws

#ifndef WOKWI  // the simulated board has no touch controller
  touch_begin();
  // Holding the screen at power-up forces a recalibration, in case a bad one
  // makes taps miss the Settings button
  bool held = calibration_boot_hold();
  if (held || !calibration_load()) calibration_run();
#endif

  display_lvgl_begin();
#ifndef WOKWI
  touch_lvgl_begin();
#endif
  if (setupMode) setup_portal_begin(net_config_has_network(net));
  else {
    mqtt_link_begin();
    nav_begin();
#ifndef WOKWI  // no touch in the simulator, so nothing could wake it
    snooze_begin();
#endif
  }
}

void loop() {
  lv_timer_handler();
  ota_loop();
  if (setupMode) setup_portal_loop();
  delay(5);
}
