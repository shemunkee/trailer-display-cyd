#include "battery_screen.h"

#include <stdio.h>

#include "settings.h"
#include "theme.h"
#include "ui.h"

using namespace theme;

// Content area is 296 x 176 inside the page padding
static constexpr int CARD_W = 100;
static constexpr int CONTENT_H = 176;

static lv_obj_t *socValue, *socBar, *stateLabel;
static lv_obj_t *volts, *amps, *watts, *timeToGo, *consumed;

void battery_screen_create(lv_obj_t* page) {
  lv_obj_t* card = ui_card(page, 0, 0, CARD_W, CONTENT_H);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(card, 6, 0);
  lv_obj_t* t = ui_text(card, "SOC", &lv_font_montserrat_10, MUTED);
  lv_obj_set_style_text_letter_space(t, 1, 0);
  socValue = ui_text(card, "--", &lv_font_montserrat_36, TEXT);

  socBar = lv_bar_create(card);
  lv_obj_set_size(socBar, CARD_W - 2 * CARD_PAD, 8);
  lv_bar_set_range(socBar, 0, 100);
  lv_obj_set_style_radius(socBar, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(socBar, 4, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(socBar, lv_color_hex(DIVIDER), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(socBar, LV_OPA_COVER, LV_PART_MAIN);

  stateLabel = ui_text(card, "", &lv_font_montserrat_12, MUTED);

  lv_obj_t* rows = ui_rows(page);
  lv_obj_set_pos(rows, CARD_W + GAP, 0);
  lv_obj_set_size(rows, 296 - CARD_W - GAP, CONTENT_H);
  volts = ui_row(rows, "Voltage");
  amps = ui_row(rows, "Current");
  watts = ui_row(rows, "Power");
  timeToGo = ui_row(rows, "Time to go");
  consumed = ui_row(rows, "Consumed", true);
}

void battery_screen_update(const VictronData& d) {
  char buf[24];
  bool charging = !isnan(d.battWatts) && d.battWatts > 0;
  bool low = !isnan(d.soc) && d.soc <= settings_get().lowSocPct;

  // SOC card
  if (isnan(d.soc)) {
    ui_set_text(socValue, "--");
    lv_bar_set_value(socBar, 0, LV_ANIM_OFF);
  } else {
    snprintf(buf, sizeof(buf), "%.0f%%", d.soc);
    ui_set_text(socValue, buf);
    int v = (int)(d.soc + 0.5f);
    if (lv_bar_get_value(socBar) != v) lv_bar_set_value(socBar, v, LV_ANIM_OFF);
  }
  ui_set_color(socValue, low ? WARN : TEXT);
  lv_color_t barColor = lv_color_hex(low ? WARN : charging ? CHARGE : TEXT);
  if (!lv_color_eq(lv_obj_get_style_bg_color(socBar, LV_PART_INDICATOR), barColor))
    lv_obj_set_style_bg_color(socBar, barColor, LV_PART_INDICATOR);

  if (isnan(d.battWatts)) ui_set_text(stateLabel, "");
  else ui_set_text(stateLabel, charging ? "Charging" : d.battWatts < 0 ? "Discharging" : "Idle");
  ui_set_color(stateLabel, charging ? CHARGE : d.battWatts < 0 ? TEXT : MUTED);

  // Rows
  if (isnan(d.battVolts)) ui_set_text(volts, "--");
  else {
    snprintf(buf, sizeof(buf), "%.1f V", d.battVolts);
    ui_set_text(volts, buf);
  }

  if (isnan(d.battAmps)) ui_set_text(amps, "--");
  else {
    snprintf(buf, sizeof(buf), "%+.1f A", d.battAmps);
    ui_set_text(amps, buf);
  }
  ui_set_color(amps, !isnan(d.battAmps) && d.battAmps > 0 ? CHARGE : TEXT);

  ui_fmt_watts(buf, sizeof(buf), d.battWatts, true);
  ui_set_text(watts, buf);
  ui_set_color(watts, charging ? CHARGE : TEXT);

  // Time to go is null while charging (SPEC 5.5)
  if (charging) {
    ui_set_text(timeToGo, "Charging");
    ui_set_color(timeToGo, MUTED);
  } else if (isnan(d.timeToGoSec)) {
    ui_set_text(timeToGo, "--");
    ui_set_color(timeToGo, TEXT);
  } else {
    long mins = (long)(d.timeToGoSec / 60);
    if (mins >= 100 * 60) snprintf(buf, sizeof(buf), "99h+");
    else snprintf(buf, sizeof(buf), "%ldh %02ldm", mins / 60, mins % 60);
    ui_set_text(timeToGo, buf);
    ui_set_color(timeToGo, TEXT);
  }

  if (isnan(d.consumedAh)) ui_set_text(consumed, "--");
  else {
    snprintf(buf, sizeof(buf), "%.0f Ah", d.consumedAh);
    ui_set_text(consumed, buf);
  }
}
