#include "overview_screen.h"

#include <stdio.h>
#include <string.h>

#include "nav.h"
#include "nav_icons.h"
#include "settings.h"
#include "tanks_screen.h"
#include "theme.h"
#include "ui.h"

using namespace theme;

// Content area is 296 x 176 inside the page padding
static constexpr int COL_W = 96;
static constexpr int CONTENT_H = 176;
static constexpr int STRIP_H = 26;  // tank strip at the bottom (mockup)
static constexpr int RING = 88;
static constexpr int RING_X = COL_W + GAP;
static constexpr int RIGHT_X = RING_X + RING + GAP;
static constexpr int SEGMENTS = 5;  // tank level as 5 bars, 20% each

struct Tile {
  lv_obj_t *card, *title, *value, *sub;
};

struct StripTank {
  lv_obj_t *box, *name, *seg[SEGMENTS];
};

static Tile acIn, solar, loads, quattro;
static lv_obj_t *ring, *socLabel, *powerLabel, *voltsLabel;
static lv_obj_t* strip;  // tank strip, shown while there are LPG tanks (SPEC 6.3)
static StripTank stripTanks[TANKS_SHOWN];
static lv_image_dsc_t stripIconDsc;
static int layoutWithStrip = -1;  // which layout is applied: 0 without, 1 with the strip

static Tile makeTile(lv_obj_t* page, int x, int y, const lv_font_t* valueFont) {
  lv_obj_t* c = ui_card(page, x, y, COL_W, (CONTENT_H - GAP) / 2);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(c, 2, 0);
  Tile t;
  t.card = c;
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

// Tiles and ring fill the height left above the strip, or all of it without tanks
static void layout(bool withStrip) {
  if ((int)withStrip == layoutWithStrip) return;
  layoutWithStrip = withStrip;
  int h = withStrip ? CONTENT_H - STRIP_H - GAP : CONTENT_H;  // 142 or 176
  int tileH = (h - GAP) / 2;                                   // 67 or 84
  lv_obj_t* tiles[] = {acIn.card, solar.card, loads.card, quattro.card};
  for (int i = 0; i < 4; i++) {
    lv_obj_set_size(tiles[i], COL_W, tileH);
    lv_obj_set_pos(tiles[i], i < 2 ? 0 : RIGHT_X, i % 2 ? tileH + GAP : 0);
  }
  lv_obj_set_pos(ring, RING_X, (tileH * 2 + GAP - RING) / 2);
  if (withStrip) lv_obj_remove_flag(strip, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(strip, LV_OBJ_FLAG_HIDDEN);
}

static void onStrip(lv_event_t*) { nav_show(Page::Tanks); }

static void plainChild(lv_obj_t* o) {
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);  // taps go to the strip
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

// Mockup: card 26px tall, tank icon, then each tank's first name word and 5 bars
static void makeStrip(lv_obj_t* page) {
  strip = ui_card(page, 0, CONTENT_H - STRIP_H, 296, STRIP_H);
  lv_obj_set_style_pad_ver(strip, 0, 0);
  lv_obj_set_style_pad_hor(strip, 10, 0);
  lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(strip, 14, 0);
  lv_obj_add_flag(strip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(strip, 4);
  lv_obj_add_event_cb(strip, onStrip, LV_EVENT_CLICKED, nullptr);

  stripIconDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  stripIconDsc.header.cf = LV_COLOR_FORMAT_A8;
  stripIconDsc.header.w = STRIP_ICON_SIZE;
  stripIconDsc.header.h = STRIP_ICON_SIZE;
  stripIconDsc.header.stride = STRIP_ICON_SIZE;
  stripIconDsc.data_size = sizeof(strip_icon_tanks);
  stripIconDsc.data = strip_icon_tanks;
  lv_obj_t* icon = lv_image_create(strip);
  lv_image_set_src(icon, &stripIconDsc);
  lv_obj_set_style_image_recolor(icon, lv_color_hex(MUTED), 0);
  lv_obj_set_style_image_recolor_opa(icon, LV_OPA_COVER, 0);

  for (StripTank& t : stripTanks) {
    t.box = lv_obj_create(strip);
    plainChild(t.box);
    lv_obj_set_size(t.box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(t.box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(t.box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(t.box, 6, 0);
    // Mockup 11px; 10px is the label size on this scale, and "Passenger" plus its bars fits
    t.name = ui_text(t.box, "", &lv_font_montserrat_10, TEXT);
    lv_obj_t* segs = lv_obj_create(t.box);
    plainChild(segs);
    lv_obj_set_size(segs, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(segs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(segs, 2, 0);
    for (lv_obj_t*& sg : t.seg) {
      sg = lv_obj_create(segs);
      plainChild(sg);
      lv_obj_set_size(sg, 11, 9);
      lv_obj_set_style_radius(sg, 2, 0);
      lv_obj_set_style_bg_opa(sg, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(sg, lv_color_hex(DIVIDER), 0);
    }
  }
}

void overview_screen_create(lv_obj_t* page) {
  acIn = makeTile(page, 0, 0, &lv_font_montserrat_18);
  solar = makeTile(page, 0, 0, &lv_font_montserrat_18);
  loads = makeTile(page, RIGHT_X, 0, &lv_font_montserrat_18);
  quattro = makeTile(page, RIGHT_X, 0, &lv_font_montserrat_14);
  lv_label_set_text(solar.title, "SOLAR");
  lv_label_set_text(loads.title, "AC LOADS");
  lv_label_set_text(quattro.title, "QUATTRO");
  lv_obj_set_style_text_color(solar.value, lv_color_hex(SOLAR), 0);
  lv_obj_set_style_text_color(loads.value, lv_color_hex(AC), 0);

  // SOC ring: a full circle that fills to the SOC
  ring = lv_arc_create(page);
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

  makeStrip(page);
  layout(false);  // until a tank is seen
}

static void updateAcInput(const VictronData& d) {
  char buf[24];
  bool shoreLive = d.acSource == 1 || d.acSource == 3;  // Grid or Shore
  bool genLive = d.acSource == 2;
  if (shoreLive) settings_set_last_ac(AcSource::Shore);  // writes NVS only on change
  if (genLive) settings_set_last_ac(AcSource::Generator);

  AcSource shown = genLive ? AcSource::Generator : shoreLive ? AcSource::Shore : settings_get().lastAc;
  ui_set_text(acIn.title, shown == AcSource::Generator ? "GENERATOR" : shown == AcSource::Shore ? "SHORE" : "AC INPUT");

  if (shoreLive || genLive) {
    ui_fmt_watts(buf, sizeof(buf), genLive ? d.gensetWatts : d.gridWatts);
    ui_set_text(acIn.value, buf);
    ui_set_color(acIn.value, genLive ? GEN : AC);
    ui_set_text(acIn.sub, genLive ? "Running" : "Connected");
  } else if (d.acSource < 0) {  // no data yet
    ui_set_text(acIn.value, "--");
    ui_set_color(acIn.value, MUTED);
    ui_set_text(acIn.sub, "");
  } else {  // 240: nothing connected; show the last one used, greyed (SPEC 6.3)
    ui_set_text(acIn.value, "0 W");
    ui_set_color(acIn.value, MUTED);
    ui_set_text(acIn.sub, shown == AcSource::None ? "Off" : "Off " LV_SYMBOL_BULLET " last used");
  }
}

static void updateRing(const VictronData& d) {
  char buf[24];
  bool charging = !isnan(d.battWatts) && d.battWatts > 0;
  bool low = !isnan(d.soc) && d.soc <= settings_get().lowSocPct;
  uint32_t color = low ? WARN : charging ? CHARGE : TEXT;

  if (isnan(d.soc)) {
    ui_set_text(socLabel, "--");
    lv_arc_set_value(ring, 0);
  } else {
    snprintf(buf, sizeof(buf), "%.0f%%", d.soc);
    ui_set_text(socLabel, buf);
    if (lv_arc_get_value(ring) != (int)(d.soc + 0.5f)) lv_arc_set_value(ring, (int)(d.soc + 0.5f));
  }
  lv_color_t ringColor = lv_color_hex(ui_color(color));
  if (!lv_color_eq(lv_obj_get_style_arc_color(ring, LV_PART_INDICATOR), ringColor))
    lv_obj_set_style_arc_color(ring, ringColor, LV_PART_INDICATOR);
  ui_set_color(socLabel, low ? WARN : TEXT);

  ui_fmt_watts(buf, sizeof(buf), d.battWatts, true);
  ui_set_text(powerLabel, isnan(d.battWatts) ? "" : buf);
  ui_set_color(powerLabel, charging ? CHARGE : MUTED);
  if (isnan(d.battVolts)) ui_set_text(voltsLabel, "");
  else {
    snprintf(buf, sizeof(buf), "%.1f V", d.battVolts);
    ui_set_text(voltsLabel, buf);
  }
}

static void updateSolar(const VictronData& d) {
  char buf[24];
  ui_fmt_watts(buf, sizeof(buf), d.pvWatts);
  ui_set_text(solar.value, buf);
  ui_set_color(solar.value, SOLAR);
  float kwh = 0;
  bool any = false;
  for (const MpptData& m : d.mppt)
    if (m.instance >= 0 && !isnan(m.yieldTodayKwh)) kwh += m.yieldTodayKwh, any = true;
  if (any) snprintf(buf, sizeof(buf), "%.1f kWh today", kwh);
  ui_set_text(solar.sub, any ? buf : "");
}

static void updateLoads(const VictronData& d) {
  char buf[24];
  ui_fmt_watts(buf, sizeof(buf), d.loadWatts);
  ui_set_text(loads.value, buf);
  ui_set_color(loads.value, AC);
  if (isnan(d.acOutAmps)) ui_set_text(loads.sub, "");
  else {
    snprintf(buf, sizeof(buf), "%.1f A", d.acOutAmps);
    ui_set_text(loads.sub, buf);
  }
}

static void updateQuattro(const VictronData& d) {
  char buf[24];
  bool charging = vebus_state_charging(d.vebusState);
  ui_set_text(quattro.value, charging ? "Charging" : vebus_state_name(d.vebusState));
  ui_set_color(quattro.value, d.vebusState == 2 ? ALARM : TEXT);  // Fault
  bool acLive = d.acSource >= 1 && d.acSource <= 3;
  if (acLive && !isnan(d.inputLimitAmps)) {
    snprintf(buf, sizeof(buf), "%.0f A limit", d.inputLimitAmps);
    ui_set_text(quattro.sub, buf);
  } else {
    ui_set_text(quattro.sub, charging ? vebus_state_name(d.vebusState) : "");  // Bulk, Absorption, ...
  }
}

// Tank strip (SPEC 6.3): first word of each name, level as 5 bars
// (lit = ceil(level / 20), 0 when empty or unknown), amber at or below 20%
static void updateTanks(const VictronData& d) {
  const TankData* shown[TANKS_SHOWN];
  int n = lpg_tanks(d, shown, TANKS_SHOWN);
  layout(n > 0);
  for (int i = 0; i < TANKS_SHOWN; i++) {
    StripTank& st = stripTanks[i];
    if (i >= n) {
      lv_obj_add_flag(st.box, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_remove_flag(st.box, LV_OBJ_FLAG_HIDDEN);
    const TankData& t = *shown[i];
    char word[sizeof(t.name)];
    strlcpy(word, t.name[0] ? t.name : "Tank", sizeof(word));
    char* sp = strchr(word, ' ');
    if (sp) *sp = 0;
    ui_set_text(st.name, word);
    ui_set_color(st.name, TEXT);
    bool ok = tank_reading_ok(t);
    int lit = ok && t.level > 0 ? (int)ceilf(t.level / 20.0f) : 0;
    if (lit > SEGMENTS) lit = SEGMENTS;
    uint32_t on = ok && t.level <= TANK_LOW_PCT ? WARN : CHARGE;
    for (int k = 0; k < SEGMENTS; k++) {
      lv_color_t c = lv_color_hex(k < lit ? ui_color(on) : DIVIDER);
      if (!lv_color_eq(lv_obj_get_style_bg_color(st.seg[k], 0), c)) lv_obj_set_style_bg_color(st.seg[k], c, 0);
    }
  }
}

void overview_screen_update(const VictronData& d) {
  updateTanks(d);
  updateAcInput(d);
  updateRing(d);
  updateSolar(d);
  updateLoads(d);
  updateQuattro(d);
}
