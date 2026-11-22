#include "settings_screen.h"

#include <string.h>

#include "board.h"
#include "calibration.h"
#include "display.h"
#include "mqtt_link.h"
#include "net_config.h"
#include "settings.h"
#include "theme.h"
#include "ui.h"

using namespace theme;

static constexpr uint32_t BUTTON_BG = 0x1A1D24;  // from the Settings mockup
static constexpr int BTN_W = 40, BTN_H = 30;
static const uint8_t SNOOZE_OPTIONS[] = {5, 10, 30, 60, 90};
static constexpr int SNOOZE_COUNT = sizeof(SNOOZE_OPTIONS);

static lv_obj_t *lowSocValue, *brightValue;
static lv_obj_t* snoozeBtn[SNOOZE_COUNT];
static lv_obj_t *modeLabel, *statusLabel;
static lv_obj_t* menu;  // Data source menu, on the top layer while open

// ---------- building blocks ----------

static lv_obj_t* row(lv_obj_t* page, const char* title) {
  lv_obj_t* c = lv_obj_create(page);
  lv_obj_remove_style_all(c);
  lv_obj_set_width(c, lv_pct(100));
  lv_obj_set_flex_grow(c, 1);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(c, lv_color_hex(CARD), 0);
  lv_obj_set_style_radius(c, RADIUS, 0);
  lv_obj_set_style_pad_left(c, 10, 0);
  lv_obj_set_style_pad_right(c, 4, 0);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* t = ui_text(c, title, &lv_font_montserrat_14, TEXT);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);
  return c;
}

// Right-aligned group of controls inside a row
static lv_obj_t* controls(lv_obj_t* r) {
  lv_obj_t* g = lv_obj_create(r);
  lv_obj_remove_style_all(g);
  lv_obj_set_size(g, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(g, 4, 0);
  lv_obj_align(g, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_remove_flag(g, LV_OBJ_FLAG_SCROLLABLE);
  return g;
}

static lv_obj_t* button(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_event_cb_t cb, intptr_t data) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, BTN_W, BTN_H);
  lv_obj_set_style_bg_color(b, lv_color_hex(BUTTON_BG), 0);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_t* l = ui_text(b, text, font, TEXT);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)data);
  return b;
}

static void stepButtonStyle(lv_obj_t* b) {
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(DIVIDER), 0);
}

// ---------- values ----------

static void showValues() {
  const Settings& s = settings_get();
  lv_label_set_text_fmt(lowSocValue, "%d%%", s.lowSocPct);
  lv_label_set_text_fmt(brightValue, "%d%%", s.brightnessPct);
  for (int i = 0; i < SNOOZE_COUNT; i++) {
    bool on = SNOOZE_OPTIONS[i] == s.snoozeSec;
    lv_obj_set_style_bg_color(snoozeBtn[i], lv_color_hex(on ? AC : BUTTON_BG), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(snoozeBtn[i], 0), lv_color_hex(on ? BG : TEXT), 0);
  }
}

enum Step { LOW_DOWN, LOW_UP, BRIGHT_DOWN, BRIGHT_UP };

static void onStep(lv_event_t* e) {
  const Settings& s = settings_get();
  switch ((intptr_t)lv_event_get_user_data(e)) {
    case LOW_DOWN: settings_set_low_soc(s.lowSocPct - 5); break;
    case LOW_UP: settings_set_low_soc(s.lowSocPct + 5); break;
    case BRIGHT_DOWN: settings_set_brightness(s.brightnessPct - 10); break;
    case BRIGHT_UP: settings_set_brightness(s.brightnessPct + 10); break;
  }
  display_set_brightness(settings_get().brightnessPct);
  showValues();
}

static void onSnooze(lv_event_t* e) {
  settings_set_snooze(SNOOZE_OPTIONS[(intptr_t)lv_event_get_user_data(e)]);
  showValues();
}

static lv_obj_t* stepper(lv_obj_t* page, const char* title, Step down, Step up) {
  lv_obj_t* g = controls(row(page, title));
  stepButtonStyle(button(g, LV_SYMBOL_MINUS, &lv_font_montserrat_14, onStep, down));
  lv_obj_t* v = ui_text(g, "", &lv_font_montserrat_14, TEXT);
  lv_obj_set_width(v, BTN_W);
  lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_CENTER, 0);
  stepButtonStyle(button(g, LV_SYMBOL_PLUS, &lv_font_montserrat_14, onStep, up));
  return v;
}

// ---------- Data source menu ----------

static void closeMenu() {
  if (menu) lv_obj_delete(menu);
  menu = nullptr;
}

static void runCalibration(void*) {
  calibration_run();  // blocking, draws on the panel directly
  display_refresh();
  lv_display_trigger_activity(nullptr);  // don't snooze straight away
}

enum MenuAction { RUN_SETUP, RECALIBRATE, CLOSE };

static void onMenu(lv_event_t* e) {
  intptr_t action = (intptr_t)lv_event_get_user_data(e);
  closeMenu();
  if (action == RUN_SETUP) net_config_request_setup();  // restarts into setup
  // After this event finishes, so LVGL isn't in the middle of handling the tap
  else if (action == RECALIBRATE) lv_async_call(runCalibration, nullptr);
}

static void menuButton(lv_obj_t* card, const char* text, MenuAction action) {
  lv_obj_t* b = lv_button_create(card);
  lv_obj_set_size(b, lv_pct(100), 36);
  lv_obj_set_style_bg_color(b, lv_color_hex(BUTTON_BG), 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(DIVIDER), 0);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t* l = ui_text(b, text, &lv_font_montserrat_14, action == CLOSE ? MUTED : TEXT);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, onMenu, LV_EVENT_CLICKED, (void*)(intptr_t)action);
}

static void openMenu(lv_event_t*) {
  if (menu) return;
  // Dimmed backdrop that also swallows taps outside the card
  menu = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(menu);
  lv_obj_set_size(menu, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(menu, lv_color_hex(BG), 0);
  lv_obj_set_style_bg_opa(menu, LV_OPA_70, 0);
  lv_obj_add_flag(menu, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(menu, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* card = lv_obj_create(menu);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, 220, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(CARD), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(DIVIDER), 0);
  lv_obj_set_style_radius(card, RADIUS, 0);
  lv_obj_set_style_pad_all(card, PAD, 0);
  lv_obj_set_style_pad_row(card, GAP, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(card);

  ui_text(card, "Data source", &lv_font_montserrat_14, TEXT);
  menuButton(card, "Run setup", RUN_SETUP);
  menuButton(card, "Recalibrate touch", RECALIBRATE);
  menuButton(card, "Close", CLOSE);
}

// ---------- public ----------

void settings_screen_create(lv_obj_t* page) {
  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(page, GAP, 0);

  lowSocValue = stepper(page, "Low SOC", LOW_DOWN, LOW_UP);

  lv_obj_t* g = controls(row(page, "Snooze"));
  for (int i = 0; i < SNOOZE_COUNT; i++) {
    char t[8];
    snprintf(t, sizeof(t), "%ds", SNOOZE_OPTIONS[i]);
    snoozeBtn[i] = button(g, t, &lv_font_montserrat_12, onSnooze, i);
  }

  brightValue = stepper(page, "Brightness", BRIGHT_DOWN, BRIGHT_UP);

  lv_obj_t* src = row(page, "Data source");
  lv_obj_add_flag(src, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(src, openMenu, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* right = controls(src);
  lv_obj_t* text = lv_obj_create(right);
  lv_obj_remove_style_all(text);
  lv_obj_set_size(text, 170, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(text, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_remove_flag(text, LV_OBJ_FLAG_CLICKABLE);
  modeLabel = ui_text(text, "", &lv_font_montserrat_12, MUTED);
  statusLabel = ui_text(text, "", &lv_font_montserrat_10, MUTED);
  lv_obj_set_width(statusLabel, 170);
  lv_obj_set_style_text_align(statusLabel, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_DOT);
  lv_obj_t* chevron = ui_text(right, LV_SYMBOL_RIGHT, &lv_font_montserrat_12, MUTED);
  lv_obj_set_style_pad_right(chevron, 4, 0);

  showValues();
  settings_screen_update();
}

void settings_screen_update() {
  LinkStatus s = mqtt_link_status();
  const char* mode = s.mode == LinkMode::Cloud ? "Cloud" : s.mode == LinkMode::Local ? "Local" : "No connection";
  uint32_t color = s.state == LinkState::Live ? CHARGE : s.mode == LinkMode::None ? MUTED : WARN;
  if (strcmp(lv_label_get_text(modeLabel), mode)) lv_label_set_text(modeLabel, mode);
  if (!lv_color_eq(lv_obj_get_style_text_color(modeLabel, 0), lv_color_hex(color)))
    lv_obj_set_style_text_color(modeLabel, lv_color_hex(color), 0);
  char buf[48];
  mqtt_link_status_text(buf, sizeof(buf));
  if (strcmp(lv_label_get_text(statusLabel), buf)) lv_label_set_text(statusLabel, buf);
}
