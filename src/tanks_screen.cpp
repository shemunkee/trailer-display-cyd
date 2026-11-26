#include "tanks_screen.h"

#include <Arduino.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "theme.h"
#include "ui.h"

using namespace theme;

// Content area is 296 x 176 inside the page padding
static constexpr int CARD_W = (296 - GAP) / 2;
static constexpr int CARD_H = 176;
static constexpr int BAR_W = 22;  // mockup level bar

struct TankCard {
  lv_obj_t *card, *title, *bar, *pct, *note, *left, *type, *status;
};

static TankCard cards[TANKS_SHOWN];
static lv_obj_t* emptyLabel;
static int loggedTotal = -1;          // LPG tank count last logged
static int loggedStatus[MAX_TANKS];   // per tank instance slot, last status logged

void tank_title(const TankData& t, char* buf, size_t len) {
  char name[sizeof(t.name)];
  strlcpy(name, t.name[0] ? t.name : "Tank", sizeof(name));
  size_t n = strlen(name);
  // Drop a trailing " Tank" (any case), as in the mockup: DRIVER SIDE
  if (n > 5 && !strcasecmp(name + n - 5, " tank")) name[n - 5] = 0;
  size_t i = 0;
  for (; name[i] && i < len - 1; i++) buf[i] = toupper((unsigned char)name[i]);
  buf[i] = 0;
}

// Small "label    value" line, 12px, as in the mockup
static lv_obj_t* line(lv_obj_t* parent, const char* label) {
  lv_obj_t* r = lv_obj_create(parent);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, lv_pct(100), 16);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* l = ui_text(r, label, &lv_font_montserrat_12, MUTED);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* v = ui_text(r, "--", &lv_font_montserrat_12, TEXT);
  lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
  return v;
}

static lv_obj_t* column(lv_obj_t* parent) {
  lv_obj_t* c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

static TankCard makeCard(lv_obj_t* page, int x) {
  TankCard k;
  k.card = ui_card(page, x, 0, CARD_W, CARD_H);
  lv_obj_set_flex_flow(k.card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(k.card, 6, 0);
  k.title = ui_text(k.card, "", &lv_font_montserrat_10, MUTED);
  lv_obj_set_style_text_letter_space(k.title, 1, 0);
  lv_obj_set_width(k.title, CARD_W - 2 * CARD_PAD);
  lv_label_set_long_mode(k.title, LV_LABEL_LONG_DOT);

  // Body: level bar on the left, numbers on the right
  lv_obj_t* body = lv_obj_create(k.card);
  lv_obj_remove_style_all(body);
  lv_obj_set_width(body, lv_pct(100));
  lv_obj_set_flex_grow(body, 1);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(body, 10, 0);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);

  // Taller than wide, so LVGL fills it from the bottom
  k.bar = lv_bar_create(body);
  lv_obj_set_size(k.bar, BAR_W, lv_pct(100));
  lv_bar_set_range(k.bar, 0, 100);
  lv_obj_set_style_bg_opa(k.bar, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(k.bar, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(k.bar, lv_color_hex(DIVIDER), LV_PART_MAIN);
  lv_obj_set_style_radius(k.bar, 9, LV_PART_MAIN);
  lv_obj_set_style_pad_all(k.bar, 2, LV_PART_MAIN);
  lv_obj_set_style_radius(k.bar, 6, LV_PART_INDICATOR);

  lv_obj_t* right = column(body);
  lv_obj_set_height(right, lv_pct(100));
  lv_obj_set_flex_grow(right, 1);
  lv_obj_set_flex_align(right, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t* top = column(right);
  lv_obj_set_size(top, lv_pct(100), LV_SIZE_CONTENT);
  // Mockup 30px; 24px is the hero size on this scale that fits "100%" beside the bar
  k.pct = ui_text(top, "--", &lv_font_montserrat_24, TEXT);
  k.note = ui_text(top, "sensor", &lv_font_montserrat_10, WARN);
  lv_obj_add_flag(k.note, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* rows = column(right);
  lv_obj_set_size(rows, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_row(rows, 4, 0);
  k.left = line(rows, "Left");
  k.type = line(rows, "Type");
  k.status = line(rows, "Status");
  lv_obj_add_flag(k.card, LV_OBJ_FLAG_HIDDEN);
  return k;
}

void tanks_screen_create(lv_obj_t* page) {
  for (int i = 0; i < TANKS_SHOWN; i++) cards[i] = makeCard(page, i * (CARD_W + GAP));
  emptyLabel = ui_text(page, "No LPG tanks found", &lv_font_montserrat_14, MUTED);
  lv_obj_align(emptyLabel, LV_ALIGN_CENTER, 0, 0);
  for (int& s : loggedStatus) s = -2;
}

static void setBar(lv_obj_t* bar, int value, uint32_t color) {
  if (lv_bar_get_value(bar) != value) lv_bar_set_value(bar, value, LV_ANIM_OFF);
  lv_color_t c = lv_color_hex(ui_color(color));
  if (!lv_color_eq(lv_obj_get_style_bg_color(bar, LV_PART_INDICATOR), c))
    lv_obj_set_style_bg_color(bar, c, LV_PART_INDICATOR);
}

static void showCard(TankCard& k, const TankData& t) {
  char buf[40];
  tank_title(t, buf, sizeof(buf));
  ui_set_text(k.title, buf);
  ui_set_text(k.type, "LPG");
  ui_set_color(k.type, TEXT);

  float lb = tank_pounds(t);
  if (isnan(lb)) ui_set_text(k.left, "--");
  else {
    snprintf(buf, sizeof(buf), "%.1f lb", lb);
    ui_set_text(k.left, buf);
  }
  ui_set_color(k.left, TEXT);

  // Status not OK, or no level: "--" with a small "sensor" note (SPEC 6.12)
  if (!tank_reading_ok(t)) {
    ui_set_text(k.pct, "--");
    ui_set_color(k.pct, MUTED);
    lv_obj_remove_flag(k.note, LV_OBJ_FLAG_HIDDEN);
    ui_set_color(k.note, WARN);
    if (t.status != 0) ui_set_text(k.left, "--");
    ui_set_text(k.status, t.status > 0 ? tank_status_name(t.status) : t.status == 0 ? "OK" : "--");
    ui_set_color(k.status, t.status > 0 ? WARN : TEXT);
    setBar(k.bar, 0, MUTED);
    return;
  }
  lv_obj_add_flag(k.note, LV_OBJ_FLAG_HIDDEN);
  bool low = t.level <= TANK_LOW_PCT;
  uint32_t color = low ? WARN : CHARGE;
  snprintf(buf, sizeof(buf), "%.0f%%", t.level);
  ui_set_text(k.pct, buf);
  ui_set_color(k.pct, color);
  ui_set_text(k.status, low ? "Low" : "OK");
  ui_set_color(k.status, color);
  int v = constrain((int)(t.level + 0.5f), 0, 100);
  setBar(k.bar, v, color);
}

// Log what the screens can't say: extra tanks, and sensor problems by their Venus name
static void logTanks(const VictronData& d, int total) {
  if (total != loggedTotal) {
    loggedTotal = total;
    if (total > TANKS_SHOWN)
      Serial.printf("Tanks: %d LPG tanks found, showing the first %d by name\n", total, TANKS_SHOWN);
  }
  for (int i = 0; i < MAX_TANKS; i++) {
    const TankData& t = d.tanks[i];
    if (t.instance < 0 || t.fluidType != FLUID_LPG || t.status == loggedStatus[i]) continue;
    loggedStatus[i] = t.status;
    if (t.status > 0) Serial.printf("Tanks: \"%s\" sensor status: %s\n", t.name, tank_status_full(t.status));
  }
}

void tanks_screen_update(const VictronData& d) {
  const TankData* shown[TANKS_SHOWN];
  int total = 0;
  int n = lpg_tanks(d, shown, TANKS_SHOWN, &total);
  logTanks(d, total);
  for (int i = 0; i < TANKS_SHOWN; i++) {
    if (i < n) {
      showCard(cards[i], *shown[i]);
      lv_obj_remove_flag(cards[i].card, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(cards[i].card, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (n) lv_obj_add_flag(emptyLabel, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(emptyLabel, LV_OBJ_FLAG_HIDDEN);
}
