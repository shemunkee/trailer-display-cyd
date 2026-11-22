#include "overview_screen.h"

#include <stdio.h>
#include <string.h>

#include "settings.h"
#include "theme.h"
#include "ui.h"

using namespace theme;

// Content area is 296 x 176 inside the page padding
static constexpr int COL_W = 96;
static constexpr int TILE_H = 84;  // (176 - 8) / 2
static constexpr int RING = 88;
static constexpr int RING_X = COL_W + GAP;
static constexpr int RING_Y = (TILE_H * 2 + GAP - RING) / 2;
static constexpr int RIGHT_X = RING_X + RING + GAP;

struct Tile {
  lv_obj_t *title, *value, *sub;
};

static Tile acIn, solar, loads, quattro;
static lv_obj_t *ring, *socLabel, *powerLabel, *voltsLabel;

static Tile makeTile(lv_obj_t* page, int x, int y, const lv_font_t* valueFont) {
  lv_obj_t* c = ui_card(page, x, y, COL_W, TILE_H);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(c, 2, 0);
  Tile t;
  t.title = ui_text(c, "", &lv_font_montserrat_10, MUTED);
  lv_obj_set_style_text_letter_space(t.title, 1, 0);
  t.value = ui_text(c, "--", valueFont, TEXT);
  t.sub = ui_text(c, "", &lv_font_montserrat_10, MUTED);
  // Clip rather than spill out of the card if a value is ever wider than expected
  lv_obj_t* labels[] = {t.title, t.value, t.sub};
  for (lv_obj_t* l : labels) {
    lv_obj_set_width(l, COL_W - 2 * CARD_PAD);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
  }
  return t;
}

// Only touch a label when its text changes, so the panel isn't redrawn every tick
static void setText(lv_obj_t* l, const char* s) {
  if (strcmp(lv_label_get_text(l), s) != 0) lv_label_set_text(l, s);
}

static void setColor(lv_obj_t* l, uint32_t c) {
  lv_color_t col = lv_color_hex(c);
  if (!lv_color_eq(lv_obj_get_style_text_color(l, 0), col)) lv_obj_set_style_text_color(l, col, 0);
}

// 640 W, 2.1 kW; "--" when unknown. sign: also show + for positive values
static void fmtWatts(char* buf, size_t len, float w, bool sign = false) {
  if (isnan(w)) snprintf(buf, len, "--");
  else if (fabsf(w) < 999.5f) snprintf(buf, len, sign ? "%+.0f W" : "%.0f W", w);
  else snprintf(buf, len, sign ? "%+.1f kW" : "%.1f kW", w / 1000.0f);
}

void overview_screen_create(lv_obj_t* page) {
  acIn = makeTile(page, 0, 0, &lv_font_montserrat_18);
  solar = makeTile(page, 0, TILE_H + GAP, &lv_font_montserrat_18);
  loads = makeTile(page, RIGHT_X, 0, &lv_font_montserrat_18);
  quattro = makeTile(page, RIGHT_X, TILE_H + GAP, &lv_font_montserrat_14);
  lv_label_set_text(solar.title, "SOLAR");
  lv_label_set_text(loads.title, "AC LOADS");
  lv_label_set_text(quattro.title, "QUATTRO");
  lv_obj_set_style_text_color(solar.value, lv_color_hex(SOLAR), 0);
  lv_obj_set_style_text_color(loads.value, lv_color_hex(AC), 0);

  // SOC ring: a full circle that fills to the SOC
  ring = lv_arc_create(page);
  lv_obj_set_pos(ring, RING_X, RING_Y);
  lv_obj_set_size(ring, RING, RING);
  lv_arc_set_rotation(ring, 270);
  lv_arc_set_bg_angles(ring, 0, 360);
  lv_arc_set_range(ring, 0, 100);
  lv_arc_set_value(ring, 0);
  lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_set_style_pad_all(ring, 0, LV_PART_KNOB);
  lv_obj_set_style_arc_width(ring, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_width(ring, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(ring, lv_color_hex(DIVIDER), LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(ring, false, LV_PART_MAIN);

  socLabel = ui_text(ring, "--", &lv_font_montserrat_24, TEXT);
  lv_obj_align(socLabel, LV_ALIGN_CENTER, 0, -12);
  powerLabel = ui_text(ring, "", &lv_font_montserrat_10, MUTED);
  lv_obj_align(powerLabel, LV_ALIGN_CENTER, 0, 8);
  voltsLabel = ui_text(ring, "", &lv_font_montserrat_10, MUTED);
  lv_obj_align(voltsLabel, LV_ALIGN_CENTER, 0, 20);
}

static void updateAcInput(const VictronData& d) {
  char buf[24];
  bool shoreLive = d.acSource == 1 || d.acSource == 3;  // Grid or Shore
  bool genLive = d.acSource == 2;
  if (shoreLive) settings_set_last_ac(AcSource::Shore);  // writes NVS only on change
  if (genLive) settings_set_last_ac(AcSource::Generator);

  AcSource shown = genLive ? AcSource::Generator : shoreLive ? AcSource::Shore : settings_get().lastAc;
  setText(acIn.title, shown == AcSource::Generator ? "GENERATOR" : shown == AcSource::Shore ? "SHORE" : "AC INPUT");

  if (shoreLive || genLive) {
    fmtWatts(buf, sizeof(buf), genLive ? d.gensetWatts : d.gridWatts);
    setText(acIn.value, buf);
    setColor(acIn.value, genLive ? GEN : AC);
    setText(acIn.sub, genLive ? "Running" : "Connected");
  } else if (d.acSource < 0) {  // no data yet
    setText(acIn.value, "--");
    setColor(acIn.value, MUTED);
    setText(acIn.sub, "");
  } else {  // 240: nothing connected; show the last one used, greyed (SPEC 6.3)
    setText(acIn.value, "0 W");
    setColor(acIn.value, MUTED);
    setText(acIn.sub, shown == AcSource::None ? "Off" : "Off " LV_SYMBOL_BULLET " last used");
  }
}

static void updateRing(const VictronData& d) {
  char buf[24];
  bool charging = !isnan(d.battWatts) && d.battWatts > 0;
  bool low = !isnan(d.soc) && d.soc <= settings_get().lowSocPct;
  uint32_t color = low ? WARN : charging ? CHARGE : TEXT;

  if (isnan(d.soc)) {
    setText(socLabel, "--");
    lv_arc_set_value(ring, 0);
  } else {
    snprintf(buf, sizeof(buf), "%.0f%%", d.soc);
    setText(socLabel, buf);
    if (lv_arc_get_value(ring) != (int)(d.soc + 0.5f)) lv_arc_set_value(ring, (int)(d.soc + 0.5f));
  }
  lv_color_t ringColor = lv_color_hex(color);
  if (!lv_color_eq(lv_obj_get_style_arc_color(ring, LV_PART_INDICATOR), ringColor))
    lv_obj_set_style_arc_color(ring, ringColor, LV_PART_INDICATOR);
  setColor(socLabel, low ? WARN : TEXT);

  fmtWatts(buf, sizeof(buf), d.battWatts, true);
  setText(powerLabel, isnan(d.battWatts) ? "" : buf);
  setColor(powerLabel, charging ? CHARGE : MUTED);
  if (isnan(d.battVolts)) setText(voltsLabel, "");
  else {
    snprintf(buf, sizeof(buf), "%.1f V", d.battVolts);
    setText(voltsLabel, buf);
  }
}

static void updateSolar(const VictronData& d) {
  char buf[24];
  fmtWatts(buf, sizeof(buf), d.pvWatts);
  setText(solar.value, buf);
  float kwh = 0;
  bool any = false;
  for (const MpptData& m : d.mppt)
    if (m.instance >= 0 && !isnan(m.yieldTodayKwh)) kwh += m.yieldTodayKwh, any = true;
  if (any) snprintf(buf, sizeof(buf), "%.1f kWh today", kwh);
  setText(solar.sub, any ? buf : "");
}

static void updateLoads(const VictronData& d) {
  char buf[24];
  fmtWatts(buf, sizeof(buf), d.loadWatts);
  setText(loads.value, buf);
  if (isnan(d.acOutAmps)) setText(loads.sub, "");
  else {
    snprintf(buf, sizeof(buf), "%.1f A", d.acOutAmps);
    setText(loads.sub, buf);
  }
}

static void updateQuattro(const VictronData& d) {
  char buf[24];
  bool charging = vebus_state_charging(d.vebusState);
  setText(quattro.value, charging ? "Charging" : vebus_state_name(d.vebusState));
  setColor(quattro.value, d.vebusState == 2 ? ALARM : TEXT);  // Fault
  bool acLive = d.acSource >= 1 && d.acSource <= 3;
  if (acLive && !isnan(d.inputLimitAmps)) {
    snprintf(buf, sizeof(buf), "%.0f A limit", d.inputLimitAmps);
    setText(quattro.sub, buf);
  } else {
    setText(quattro.sub, charging ? vebus_state_name(d.vebusState) : "");  // Bulk, Absorption, ...
  }
}

void overview_screen_update(const VictronData& d) {
  updateAcInput(d);
  updateRing(d);
  updateSolar(d);
  updateLoads(d);
  updateQuattro(d);
}
