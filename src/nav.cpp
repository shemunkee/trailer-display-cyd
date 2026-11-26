#include "nav.h"

#include <Arduino.h>
#include <string.h>

#include "ac_screen.h"
#include "alarms_screen.h"
#include "battery_screen.h"
#include "board.h"
#include "mqtt_link.h"
#include "nav_icons.h"
#include "overview_screen.h"
#include "settings.h"
#include "settings_screen.h"
#include "snooze.h"
#include "solar_screen.h"
#include "tanks_screen.h"
#include "theme.h"
#include "ui.h"
#include "update_ui.h"
#include "victron_data.h"

using namespace theme;

static constexpr uint32_t UPDATE_MS = 500;
static constexpr uint32_t STALE_MS = 30000;  // SPEC 5.6
static constexpr int PAGE_COUNT = (int)Page::Count;

struct NavButton {
  lv_obj_t* btn;
  lv_obj_t* icon;
  lv_obj_t* bar;  // blue bar on top of the active button
};

static lv_obj_t* pages[PAGE_COUNT];
static NavButton buttons[PAGE_COUNT];
static lv_obj_t* alarmDot;
static lv_obj_t* staleMarker;  // NO CONNECTION / STALE / NO DATA, top right on data screens
static const char* staleText = nullptr;
static lv_image_dsc_t iconDsc[PAGE_COUNT];
static Page current = Page::Overview;
static lv_timer_t* updateTimer;

static const uint8_t* const ICON_DATA[PAGE_COUNT] = {
    nav_icon_home, nav_icon_battery, nav_icon_solar, nav_icon_ac, nav_icon_tanks, nav_icon_alarms, nav_icon_settings,
};
static uint32_t alarmSerialSeen = 0;  // newest device alarm we've already jumped to

// Every screen but Settings shows data, so greys out when it's stale (SPEC 5.6)
static bool isDataPage(Page p) { return p != Page::Settings; }

static void showMarker() {
  if (staleText && isDataPage(current)) {
    if (strcmp(lv_label_get_text(staleMarker), staleText)) lv_label_set_text(staleMarker, staleText);
    lv_obj_remove_flag(staleMarker, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(staleMarker, LV_OBJ_FLAG_HIDDEN);
  }
}

void nav_show(Page p) {
  current = p;
  showMarker();
  for (int i = 0; i < PAGE_COUNT; i++) {
    bool on = i == (int)p;
    if (on) lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(buttons[i].btn, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_image_recolor(buttons[i].icon, lv_color_hex(on ? TEXT : MUTED), 0);
    if (on) lv_obj_remove_flag(buttons[i].bar, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(buttons[i].bar, LV_OBJ_FLAG_HIDDEN);
  }
}

void nav_refresh() {
  if (updateTimer) lv_timer_ready(updateTimer);
}

static void onNav(lv_event_t* e) { nav_show((Page)(intptr_t)lv_event_get_user_data(e)); }

static lv_obj_t* makePage(lv_obj_t* scr) {
  lv_obj_t* p = lv_obj_create(scr);
  lv_obj_remove_style_all(p);
  lv_obj_set_pos(p, 0, 0);
  lv_obj_set_size(p, SCREEN_W, SCREEN_H - NAV_H);
  lv_obj_set_style_pad_all(p, PAD, 0);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
  return p;
}

static void buildBar(lv_obj_t* scr) {
  lv_obj_t* bar = lv_obj_create(scr);
  lv_obj_remove_style_all(bar);
  lv_obj_set_pos(bar, 0, SCREEN_H - NAV_H);
  lv_obj_set_size(bar, SCREEN_W, NAV_H);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_color(bar, lv_color_hex(DIVIDER), 0);
  lv_obj_set_style_pad_hor(bar, PAD, 0);
  lv_obj_set_style_pad_ver(bar, 4, 0);
  lv_obj_set_style_pad_column(bar, 3, 0);  // as in the mockup; seven buttons of about 40px
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < PAGE_COUNT; i++) {
    NavButton& b = buttons[i];
    b.btn = lv_obj_create(bar);
    lv_obj_remove_style_all(b.btn);
    lv_obj_set_flex_grow(b.btn, 1);
    lv_obj_set_height(b.btn, 32);
    lv_obj_set_style_radius(b.btn, 6, 0);
    lv_obj_set_style_bg_color(b.btn, lv_color_hex(CARD), 0);
    lv_obj_add_flag(b.btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(b.btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b.btn, onNav, LV_EVENT_CLICKED, (void*)(intptr_t)i);

    iconDsc[i].header.magic = LV_IMAGE_HEADER_MAGIC;
    iconDsc[i].header.cf = LV_COLOR_FORMAT_A8;
    iconDsc[i].header.w = NAV_ICON_SIZE;
    iconDsc[i].header.h = NAV_ICON_SIZE;
    iconDsc[i].header.stride = NAV_ICON_SIZE;
    iconDsc[i].data_size = NAV_ICON_SIZE * NAV_ICON_SIZE;
    iconDsc[i].data = ICON_DATA[i];
    b.icon = lv_image_create(b.btn);
    lv_image_set_src(b.icon, &iconDsc[i]);
    lv_obj_set_style_image_recolor_opa(b.icon, LV_OPA_COVER, 0);
    lv_obj_center(b.icon);

    b.bar = lv_obj_create(b.btn);
    lv_obj_remove_style_all(b.bar);
    lv_obj_remove_flag(b.bar, LV_OBJ_FLAG_CLICKABLE);  // taps go to the button
    lv_obj_set_size(b.bar, 20, 2);  // mockup: 10px in from each side of a ~40px button
    lv_obj_set_style_radius(b.bar, 1, 0);
    lv_obj_set_style_bg_opa(b.bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b.bar, lv_color_hex(AC), 0);
    lv_obj_align(b.bar, LV_ALIGN_TOP_MID, 0, 0);
  }

  alarmDot = lv_obj_create(buttons[(int)Page::Alarms].btn);
  lv_obj_remove_style_all(alarmDot);
  lv_obj_remove_flag(alarmDot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(alarmDot, 7, 7);
  lv_obj_set_style_radius(alarmDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(alarmDot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(alarmDot, lv_color_hex(ALARM), 0);
  lv_obj_align(alarmDot, LV_ALIGN_TOP_RIGHT, -9, 5);  // mockup: top 5, right 9
  lv_obj_add_flag(alarmDot, LV_OBJ_FLAG_HIDDEN);
}

// Whole-system freshness (SPEC 5.6): the Cerbo only sends a value when it
// changes, so a steady group (shore at 0 W, Quattro state) goes quiet while
// still correct. Data counts as fresh while the link is up and anything at all
// arrived in the last 30 s; battery voltage and current change every second or two.
static const char* staleState(const VictronData& d) {
  LinkStatus ls = mqtt_link_status();
  bool linked = ls.state == LinkState::Live || ls.state == LinkState::Loading;
  if (!linked) return "NO CONNECTION";
  if (!d.lastMessageMs) return ls.state == LinkState::Live ? "NO DATA" : nullptr;
  if (millis() - d.lastMessageMs > STALE_MS) return "STALE";
  return nullptr;
}

static void onUpdate(lv_timer_t*) {
  VictronData d;
  victron_data_get(d);
  staleText = staleState(d);
  showMarker();
  ui_set_dim(staleText != nullptr);
  overview_screen_update(d);
  battery_screen_update(d);
  solar_screen_update(d);
  ac_screen_update(d);
  tanks_screen_update(d);
  int active = alarms_screen_update(d);
  ui_set_dim(false);
  settings_screen_update();
  update_ui_tick(isDataPage(current));

  // Alarms dot (SPEC 6.2, 6.11): any device alarm or warning, or low SOC
  if (active) lv_obj_remove_flag(alarmDot, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(alarmDot, LV_OBJ_FLAG_HIDDEN);

  // A new (or worse) device alarm wakes the screen and shows Alarms, once per
  // alarm (SPEC 6.11). Low SOC only turns amber and shows the dot.
  if (d.alarmSerial != alarmSerialSeen) {
    alarmSerialSeen = d.alarmSerial;
    Serial.println("Alarms: new device alarm, showing Alarms");
    snooze_wake();
    nav_show(Page::Alarms);
  }
}

void nav_begin() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < PAGE_COUNT; i++) pages[i] = makePage(scr);
  overview_screen_create(pages[(int)Page::Overview]);
  battery_screen_create(pages[(int)Page::Battery]);
  solar_screen_create(pages[(int)Page::Solar]);
  ac_screen_create(pages[(int)Page::Ac]);
  tanks_screen_create(pages[(int)Page::Tanks]);
  settings_screen_create(pages[(int)Page::Settings]);
  alarms_screen_create(pages[(int)Page::Alarms]);

  buildBar(scr);
  // In the top padding, so it covers nothing; on the screen so it isn't dimmed
  staleMarker = ui_text(scr, "", &lv_font_montserrat_10, WARN);
  lv_obj_set_style_text_letter_space(staleMarker, 1, 0);
  lv_obj_align(staleMarker, LV_ALIGN_TOP_RIGHT, -PAD, 0);
  lv_obj_add_flag(staleMarker, LV_OBJ_FLAG_HIDDEN);
  update_ui_create(scr);
  nav_show(Page::Overview);
  onUpdate(nullptr);
  updateTimer = lv_timer_create(onUpdate, UPDATE_MS, nullptr);

  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  Serial.printf("UI: built, LVGL memory %u%% used (%u of %u bytes free)\n", mon.used_pct,
                (unsigned)mon.free_size, (unsigned)mon.total_size);
}
