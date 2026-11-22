#include "settings_screen.h"

#include <string.h>

#include "board.h"
#include "calibration.h"
#include "display.h"
#include "mqtt_link.h"
#include "net_config.h"
#include "ota.h"
#include "settings.h"
#include "theme.h"
#include "ui.h"
#include "update_ui.h"

using namespace theme;

static constexpr uint32_t BUTTON_BG = 0x1A1D24;  // from the Settings mockup
static constexpr int BTN_W = 40, BTN_H = 26;  // five rows fit in 176 px with 6 px gaps
static constexpr int ROW_GAP = 6;
static const uint8_t SNOOZE_OPTIONS[] = {5, 10, 30, 60, 90};
static constexpr int SNOOZE_COUNT = sizeof(SNOOZE_OPTIONS);

static lv_obj_t *lowSocValue, *brightValue;
static lv_obj_t* snoozeBtn[SNOOZE_COUNT];
static lv_obj_t* usbBtn[2];  // Left (flipped), Right (normal)
static lv_obj_t *modeLabel, *statusLabel;
static lv_obj_t* menu;  // Data source menu, on the top layer while open
static lv_obj_t *updateBtnLabel, *updateStatus;  // in the menu, while open
static bool checkWaiting = false;  // "Check for updates" tapped, result not in yet
static uint16_t checksAtTap = 0;
static String displayName;  // from setup, shown in the menu so displays can be told apart

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
  // Flex row: title, a spacer that takes the free width, then the controls.
  // (Aligning a content-sized group to the right edge placed it before its
  // size was known, so on the device it sat off to the right and low, clipped.)
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  ui_text(c, title, &lv_font_montserrat_14, TEXT);
  lv_obj_t* spacer = lv_obj_create(c);
  lv_obj_remove_style_all(spacer);
  lv_obj_set_height(spacer, 1);
  lv_obj_set_flex_grow(spacer, 1);
  lv_obj_remove_flag(spacer, LV_OBJ_FLAG_CLICKABLE);
  return c;
}

// Group of controls at the right end of a row
static lv_obj_t* controls(lv_obj_t* r) {
  lv_obj_t* g = lv_obj_create(r);
  lv_obj_remove_style_all(g);
  lv_obj_set_size(g, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(g, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(g, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(g, 4, 0);
  lv_obj_remove_flag(g, LV_OBJ_FLAG_SCROLLABLE);
  // Not clickable, so a tap between buttons (or on the Data source text and
  // chevron) goes to the row underneath
  lv_obj_remove_flag(g, LV_OBJ_FLAG_CLICKABLE);
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
  for (int i = 0; i < 2; i++) {
    bool on = (i == 0) == s.flipped;
    lv_obj_set_style_bg_color(usbBtn[i], lv_color_hex(on ? AC : BUTTON_BG), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(usbBtn[i], 0), lv_color_hex(on ? BG : TEXT), 0);
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

// Turns the screen 180 degrees right away; touch follows, no recalibration
static void onUsb(lv_event_t* e) {
  bool flip = (intptr_t)lv_event_get_user_data(e) == 0;
  if (flip == settings_get().flipped) return;
  settings_set_flipped(flip);
  display_set_flipped(flip);
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
  updateBtnLabel = updateStatus = nullptr;
}

static void runCalibration(void*) {
  calibration_run();  // blocking, draws on the panel directly
  display_refresh();
  lv_display_trigger_activity(nullptr);  // don't snooze straight away
}

enum MenuAction { RUN_SETUP, RECALIBRATE, UPDATES, CLOSE };

static bool updateReady(const OtaStatus& st) {
  return st.state == OtaState::Available || st.state == OtaState::InstallFailed;
}

static void onMenu(lv_event_t* e) {
  intptr_t action = (intptr_t)lv_event_get_user_data(e);
  if (action == UPDATES) {  // SPEC 5.7
    OtaStatus st = ota_status();
    if (updateReady(st)) {
      closeMenu();
      update_ui_confirm();
    } else if (st.state != OtaState::Off && st.state != OtaState::Checking) {
      checksAtTap = st.checksDone;
      checkWaiting = true;
      ota_request_check();  // the menu stays open to show the result
    }
    return;
  }
  closeMenu();
  if (action == RUN_SETUP) net_config_request_setup();  // restarts into setup
  // After this event finishes, so LVGL isn't in the middle of handling the tap
  else if (action == RECALIBRATE) lv_async_call(runCalibration, nullptr);
}

static lv_obj_t* menuButton(lv_obj_t* card, const char* text, MenuAction action) {
  lv_obj_t* b = lv_button_create(card);
  lv_obj_set_size(b, lv_pct(100), 32);
  lv_obj_set_style_bg_color(b, lv_color_hex(BUTTON_BG), 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(DIVIDER), 0);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t* l = ui_text(b, text, &lv_font_montserrat_14, action == CLOSE ? MUTED : TEXT);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, onMenu, LV_EVENT_CLICKED, (void*)(intptr_t)action);
  return l;
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

  lv_obj_t* head = ui_text(card, "Data source", &lv_font_montserrat_14, TEXT);
  if (!displayName.isEmpty()) {  // "Data source · Sabrina"
    String t = "Data source " LV_SYMBOL_BULLET " " + displayName;
    lv_label_set_text(head, t.c_str());
    lv_obj_set_width(head, lv_pct(100));
    lv_label_set_long_mode(head, LV_LABEL_LONG_DOT);
  }
  menuButton(card, "Run setup", RUN_SETUP);
  menuButton(card, "Recalibrate touch", RECALIBRATE);
  updateBtnLabel = menuButton(card, "Check for updates", UPDATES);
  updateStatus = ui_text(card, "", &lv_font_montserrat_10, MUTED);
  lv_obj_set_width(updateStatus, lv_pct(100));
  lv_obj_set_style_text_align(updateStatus, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(updateStatus, LV_LABEL_LONG_WRAP);
  menuButton(card, "Close", CLOSE);
  settings_screen_update();
}

// Version and update state under the button, while the menu is open
static void showUpdateState() {
  if (!menu) return;
  OtaStatus st = ota_status();
  if (checkWaiting && st.checksDone != checksAtTap) checkWaiting = false;
  char btn[32], line[80];
  if (updateReady(st)) snprintf(btn, sizeof(btn), "Install %s", st.latest);
  else snprintf(btn, sizeof(btn), "Check for updates");
  const char* v = ota_version();
  if (st.state == OtaState::Checking || checkWaiting) snprintf(line, sizeof(line), "Firmware %s " LV_SYMBOL_BULLET " checking...", v);
  else switch (st.state) {
    case OtaState::Off: snprintf(line, sizeof(line), "Firmware %s " LV_SYMBOL_BULLET " updates not set up", v); break;
    case OtaState::UpToDate: snprintf(line, sizeof(line), "Firmware %s " LV_SYMBOL_BULLET " up to date", v); break;
    case OtaState::Available: snprintf(line, sizeof(line), "Firmware %s " LV_SYMBOL_BULLET " %s available", v, st.latest); break;
    case OtaState::CheckFailed: snprintf(line, sizeof(line), "Firmware %s " LV_SYMBOL_BULLET " check failed: %s", v, st.message); break;
    case OtaState::InstallFailed: snprintf(line, sizeof(line), "Install failed: %s", st.message); break;
    default:
      snprintf(line, sizeof(line), "Firmware %s%s", v, st.rolledBack ? " " LV_SYMBOL_BULLET " last update was undone" : "");
      break;
  }
  ui_set_text(updateBtnLabel, btn);
  ui_set_text(updateStatus, line);
}

// ---------- public ----------

void settings_screen_create(lv_obj_t* page) {
  NetConfig net;
  net_config_load(net);
  displayName = net.deviceName;

  lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(page, ROW_GAP, 0);

  lowSocValue = stepper(page, "Low SOC", LOW_DOWN, LOW_UP);

  lv_obj_t* g = controls(row(page, "Snooze"));
  for (int i = 0; i < SNOOZE_COUNT; i++) {
    char t[8];
    snprintf(t, sizeof(t), "%ds", SNOOZE_OPTIONS[i]);
    snoozeBtn[i] = button(g, t, &lv_font_montserrat_12, onSnooze, i);
  }

  brightValue = stepper(page, "Brightness", BRIGHT_DOWN, BRIGHT_UP);

  lv_obj_t* usb = controls(row(page, "USB side"));
  usbBtn[0] = button(usb, "Left", &lv_font_montserrat_12, onUsb, 0);
  usbBtn[1] = button(usb, "Right", &lv_font_montserrat_12, onUsb, 1);
  for (lv_obj_t* b : usbBtn) lv_obj_set_width(b, 48);

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
  ui_set_text(modeLabel, mode);
  if (!lv_color_eq(lv_obj_get_style_text_color(modeLabel, 0), lv_color_hex(color)))
    lv_obj_set_style_text_color(modeLabel, lv_color_hex(color), 0);
  char buf[48];
  mqtt_link_status_text(buf, sizeof(buf));
  ui_set_text(statusLabel, buf);
  showUpdateState();
}
