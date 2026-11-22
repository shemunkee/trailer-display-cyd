#include "demo_screen.h"

#include <lvgl.h>

#include "board.h"
#include "display.h"
#include "mqtt_local.h"
#include "net_config.h"
#include "settings.h"
#include "theme.h"
#include "touch.h"
#include "ui.h"

using namespace theme;

enum Stepper { BRIGHTNESS, LOW_SOC, SNOOZE };

static lv_obj_t* valueLabel[3];
static lv_obj_t* touchLabel;
static lv_obj_t* marker;
static lv_obj_t* linkLabel;

static void showValues() {
  const Settings& s = settings_get();
  lv_label_set_text_fmt(valueLabel[BRIGHTNESS], "%d%%", s.brightnessPct);
  lv_label_set_text_fmt(valueLabel[LOW_SOC], "%d%%", s.lowSocPct);
  lv_label_set_text_fmt(valueLabel[SNOOZE], "%ds", s.snoozeSec);
}

// user_data packs the stepper id and direction: id * 2 + (up ? 1 : 0)
static void onStep(lv_event_t* e) {
  int code = (int)(intptr_t)lv_event_get_user_data(e);
  int id = code / 2, dir = (code & 1) ? 1 : -1;
  const Settings& s = settings_get();
  switch (id) {
    case BRIGHTNESS:
      settings_set_brightness(s.brightnessPct + dir * 10);
      display_set_brightness(settings_get().brightnessPct);
      break;
    case LOW_SOC: settings_set_low_soc(s.lowSocPct + dir * 5); break;
    case SNOOZE: settings_set_snooze(settings_snooze_step(s.snoozeSec, dir)); break;
  }
  showValues();
}

static lv_obj_t* stepButton(lv_obj_t* parent, const char* sym, int x, int y, int code) {
  lv_obj_t* b = ui_button(parent, sym, x, y, 32, 24);
  lv_obj_add_event_cb(b, onStep, LV_EVENT_CLICKED, (void*)(intptr_t)code);
  return b;
}

// Title on the left, then [-] value [+] on the right, all on one 24px row
static void stepperRow(lv_obj_t* parent, int y, const char* title, Stepper id) {
  lv_obj_t* t = ui_text(parent, title, &lv_font_montserrat_12, MUTED);
  lv_obj_set_pos(t, 0, y + 5);
  stepButton(parent, LV_SYMBOL_MINUS, 56, y, id * 2);
  stepButton(parent, LV_SYMBOL_PLUS, 124, y, id * 2 + 1);
  valueLabel[id] = ui_text(parent, "", &lv_font_montserrat_14, TEXT);
  lv_obj_set_width(valueLabel[id], 36);
  lv_obj_set_style_text_align(valueLabel[id], LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(valueLabel[id], 88, y + 4);
}

static void onTimer(lv_timer_t*) {
  int16_t x, y;
  if (touch_last(x, y)) {
    lv_obj_set_pos(marker, x - 5, y - 5);
    lv_obj_remove_flag(marker, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(touchLabel, "x %d  y %d", x, y);
  } else {
    lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
  }
}

static void onLinkTimer(lv_timer_t*) {
  char buf[64];
  mqtt_local_status_text(buf, sizeof(buf));
  lv_label_set_text(linkLabel, buf);
}

void demo_screen_create() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(BG), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // Left: type scale samples, edge to edge on the 12px padding
  const int leftW = 112;
  lv_obj_t* types = ui_card(scr, PAD, PAD, leftW, SCREEN_H - 2 * PAD);
  lv_obj_set_flex_flow(types, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(types, 6, 0);
  ui_text(types, "TYPE SCALE", &lv_font_montserrat_10, MUTED);
  ui_text(types, "10px label", &lv_font_montserrat_10, MUTED);
  ui_text(types, "12px row text", &lv_font_montserrat_12, TEXT);
  ui_text(types, "14px value", &lv_font_montserrat_14, TEXT);
  ui_text(types, "18px tile", &lv_font_montserrat_18, TEXT);
  ui_text(types, "87%", &lv_font_montserrat_40, CHARGE);
  lv_obj_t* colors = lv_obj_create(types);
  lv_obj_remove_style_all(colors);
  lv_obj_set_size(colors, LV_PCT(100), 12);
  lv_obj_set_flex_flow(colors, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(colors, 4, 0);
  static const uint32_t swatches[] = {SOLAR, AC, CHARGE, GEN, WARN, ALARM};
  for (uint32_t c : swatches) {
    lv_obj_t* sw = lv_obj_create(colors);
    lv_obj_remove_style_all(sw);
    lv_obj_set_size(sw, 12, 12);
    lv_obj_set_style_radius(sw, 3, 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(sw, lv_color_hex(c), 0);
  }

  // Right: settings that save to NVS
  const int rightX = PAD + leftW + GAP;
  const int rightW = SCREEN_W - PAD - rightX;
  lv_obj_t* set = ui_card(scr, rightX, PAD, rightW, 128);
  ui_text(set, "SETTINGS (SAVED IN NVS)", &lv_font_montserrat_10, MUTED);
  stepperRow(set, 18, "Bright", BRIGHTNESS);
  stepperRow(set, 50, "Low SOC", LOW_SOC);
  stepperRow(set, 82, "Snooze", SNOOZE);
  showValues();

  // Right bottom: last touch position
  const int touchY = PAD + 128 + GAP;
  lv_obj_t* tc = ui_card(scr, rightX, touchY, rightW, SCREEN_H - PAD - touchY);
  ui_text(tc, "TOUCH", &lv_font_montserrat_10, MUTED);
  touchLabel = ui_text(tc, "Tap", &lv_font_montserrat_14, TEXT);
  lv_obj_set_pos(touchLabel, 0, 16);
  lv_obj_t* setupBtn = ui_button(tc, "Run setup", 88, 10, 72, 24);
  lv_obj_add_event_cb(setupBtn, [](lv_event_t*) { net_config_request_setup(); }, LV_EVENT_CLICKED, nullptr);
  // Local MQTT status, two lines at most
  linkLabel = ui_text(tc, "", &lv_font_montserrat_10, MUTED);
  lv_obj_set_width(linkLabel, LV_PCT(100));
  lv_obj_set_pos(linkLabel, 0, 40);

  // Marker that follows the finger, above everything and not clickable
  marker = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(marker);
  lv_obj_set_size(marker, 10, 10);
  lv_obj_set_style_radius(marker, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(marker, lv_color_hex(ALARM), 0);
  lv_obj_remove_flag(marker, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);

  lv_timer_create(onTimer, 30, nullptr);
  lv_timer_create(onLinkTimer, 500, nullptr);
  onLinkTimer(nullptr);
}
