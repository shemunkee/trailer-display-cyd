// CYD Victron display (see SPEC.md)
// Built so far: display, touch with saved calibration, settings in NVS, setup portal, local MQTT.

#include <Arduino.h>
#include <lvgl.h>

#include "calibration.h"
#include "demo_screen.h"
#include "display.h"
#include "mqtt_local.h"
#include "net_config.h"
#include "settings.h"
#include "setup_portal.h"
#include "touch.h"

static bool setupMode = false;

void setup() {
  Serial.begin(115200);
  Serial.println("\nCYD Victron display starting");

  settings_load();
  NetConfig net;
  net_config_load(net);
  net_config_log(net);
  // Setup opens when nothing is saved yet, or when "Run setup" asked for it
  setupMode = net_config_take_setup_request() || !net_config_has_network(net);

  display_begin();
  display_set_brightness(settings_get().brightnessPct);

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
    mqtt_local_begin();
    demo_screen_create();
  }
}

void loop() {
  lv_timer_handler();
  if (setupMode) setup_portal_loop();
  delay(5);
}
