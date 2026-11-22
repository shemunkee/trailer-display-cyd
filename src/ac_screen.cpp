#include "ac_screen.h"

#include <stdio.h>

#include "settings.h"
#include "theme.h"
#include "ui.h"

using namespace theme;

// Content area is 296 x 176 inside the page padding
static constexpr int STATE_H = 34;
static constexpr int CARD_W = (296 - GAP) / 2;
static constexpr int CARD_H = 176 - STATE_H - GAP;

struct AcCard {
  lv_obj_t *title, *big, *r1, *r2, *r3;
};

static AcCard in, out;
static lv_obj_t* stateValue;

static AcCard makeCard(lv_obj_t* page, int x, const char* title, const char* l1, const char* l2, const char* l3) {
  lv_obj_t* c = ui_card(page, x, 0, CARD_W, CARD_H);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(c, 2, 0);
  AcCard k;
  k.title = ui_text(c, title, &lv_font_montserrat_10, MUTED);
  lv_obj_set_style_text_letter_space(k.title, 1, 0);
  k.big = ui_text(c, "--", &lv_font_montserrat_18, TEXT);
  lv_obj_t* rows = ui_rows(c);
  k.r1 = ui_row(rows, l1);
  k.r2 = ui_row(rows, l2);
  k.r3 = ui_row(rows, l3, true);
  return k;
}

void ac_screen_create(lv_obj_t* page) {
  in = makeCard(page, 0, "AC IN", "Voltage", "Power", "Limit");
  out = makeCard(page, CARD_W + GAP, "AC OUT", "Voltage", "Current", "Freq");
  lv_obj_set_style_text_color(out.big, lv_color_hex(AC), 0);

  lv_obj_t* bar = ui_card(page, 0, CARD_H + GAP, 296, STATE_H);
  lv_obj_t* l = ui_text(bar, "Quattro state", &lv_font_montserrat_12, MUTED);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  stateValue = ui_text(bar, "--", &lv_font_montserrat_14, TEXT);
  lv_obj_align(stateValue, LV_ALIGN_RIGHT_MID, 0, 0);
}

static void showNum(lv_obj_t* l, float v, const char* fmt) {
  char buf[24];
  if (isnan(v)) ui_set_text(l, "--");
  else {
    snprintf(buf, sizeof(buf), fmt, v);
    ui_set_text(l, buf);
  }
}

void ac_screen_update(const VictronData& d) {
  char buf[24];
  // Same rules as the Overview AC input tile (SPEC 6.3); Overview keeps lastAc saved
  bool shoreLive = d.acSource == 1 || d.acSource == 3;
  bool genLive = d.acSource == 2;
  bool live = shoreLive || genLive;
  AcSource shown = genLive ? AcSource::Generator : shoreLive ? AcSource::Shore : settings_get().lastAc;
  ui_set_text(in.title, shown == AcSource::Generator ? "GENERATOR IN" : shown == AcSource::Shore ? "SHORE IN" : "AC IN");

  if (live) {
    ui_set_text(in.big, genLive ? "Running" : "Connected");
    ui_set_color(in.big, genLive ? GEN : AC);
  } else {
    ui_set_text(in.big, d.acSource < 0 ? "--" : "Off");
    ui_set_color(in.big, MUTED);
  }
  showNum(in.r1, d.acInVolts, "%.0f V");
  ui_fmt_watts(buf, sizeof(buf), shown == AcSource::Generator ? d.gensetWatts : d.gridWatts);
  ui_set_text(in.r2, buf);
  // Greyed while nothing is connected, as in the mockup; the limit still applies
  ui_set_color(in.r1, live ? TEXT : MUTED);
  ui_set_color(in.r2, live ? TEXT : MUTED);
  showNum(in.r3, d.inputLimitAmps, "%.0f A");

  ui_fmt_watts(buf, sizeof(buf), d.loadWatts);
  ui_set_text(out.big, buf);
  showNum(out.r1, d.acOutVolts, "%.0f V");
  showNum(out.r2, d.acOutAmps, "%.1f A");
  showNum(out.r3, d.acOutHz, "%.0f Hz");

  // The full state name here (Bulk, Absorption, ...), unlike the Overview tile
  ui_set_text(stateValue, vebus_state_name(d.vebusState));
  ui_set_color(stateValue, d.vebusState == 2 ? ALARM : TEXT);
}
