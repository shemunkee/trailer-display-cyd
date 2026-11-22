#include "nav.h"

#include <Arduino.h>

#include "ac_screen.h"
#include "battery_screen.h"
#include "board.h"
#include "nav_icons.h"
#include "overview_screen.h"
#include "settings.h"
#include "settings_screen.h"
#include "solar_screen.h"
#include "theme.h"
#include "ui.h"
#include "victron_data.h"

using namespace theme;

static constexpr uint32_t UPDATE_MS = 500;
static constexpr int PAGE_COUNT = (int)Page::Count;

struct NavButton {
  lv_obj_t* btn;
  lv_obj_t* icon;
  lv_obj_t* bar;  // blue bar on top of the active button
};

static lv_obj_t* pages[PAGE_COUNT];
static NavButton buttons[PAGE_COUNT];
static lv_obj_t* alarmDot;
static lv_image_dsc_t iconDsc[PAGE_COUNT];
static Page current = Page::Overview;

static const uint8_t* const ICON_DATA[PAGE_COUNT] = {
    nav_icon_home, nav_icon_battery, nav_icon_solar, nav_icon_ac, nav_icon_alarms, nav_icon_settings,
};
static const char* const PAGE_NAMES[PAGE_COUNT] = {"Overview", "Battery", "Solar", "AC / Quattro", "Alarms",
                                                   "Settings"};

void nav_show(Page p) {
  current = p;
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

// Alarms comes with checklist item 13
static void placeholder(lv_obj_t* page, const char* name) {
  lv_obj_t* l = ui_text(page, name, &lv_font_montserrat_14, TEXT);
  lv_obj_align(l, LV_ALIGN_CENTER, 0, -10);
  lv_obj_t* s = ui_text(page, "Coming soon", &lv_font_montserrat_12, MUTED);
  lv_obj_align(s, LV_ALIGN_CENTER, 0, 10);
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
  lv_obj_set_style_pad_column(bar, 4, 0);
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
    lv_obj_set_size(b.bar, 22, 2);
    lv_obj_set_style_radius(b.bar, 1, 0);
    lv_obj_set_style_bg_opa(b.bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b.bar, lv_color_hex(AC), 0);
    lv_obj_align(b.bar, LV_ALIGN_TOP_MID, 0, 0);
  }

  alarmDot = lv_obj_create(buttons[(int)Page::Alarms].btn);
  lv_obj_remove_style_all(alarmDot);
  lv_obj_set_size(alarmDot, 7, 7);
  lv_obj_set_style_radius(alarmDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(alarmDot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(alarmDot, lv_color_hex(ALARM), 0);
  lv_obj_align(alarmDot, LV_ALIGN_TOP_RIGHT, -13, 5);
  lv_obj_add_flag(alarmDot, LV_OBJ_FLAG_HIDDEN);
}

static void onUpdate(lv_timer_t*) {
  VictronData d;
  victron_data_get(d);
  overview_screen_update(d);
  battery_screen_update(d);
  solar_screen_update(d);
  ac_screen_update(d);
  settings_screen_update();
  // Alarms dot (SPEC 6.11): low SOC for now; device alarms come in item 13
  bool alarm = !isnan(d.soc) && d.soc <= settings_get().lowSocPct;
  if (alarm) lv_obj_remove_flag(alarmDot, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(alarmDot, LV_OBJ_FLAG_HIDDEN);
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
  settings_screen_create(pages[(int)Page::Settings]);
  placeholder(pages[(int)Page::Alarms], PAGE_NAMES[(int)Page::Alarms]);

  buildBar(scr);
  nav_show(Page::Overview);
  onUpdate(nullptr);
  lv_timer_create(onUpdate, UPDATE_MS, nullptr);

  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  Serial.printf("UI: built, LVGL memory %u%% used (%u of %u bytes free)\n", mon.used_pct,
                (unsigned)mon.free_size, (unsigned)mon.total_size);
}
