#include "solar_screen.h"

#include <ctype.h>
#include <stdio.h>

#include "theme.h"
#include "ui.h"

using namespace theme;

// Content area is 296 x 176 inside the page padding
static constexpr int HEADER_H = 24;
static constexpr int CARD_Y = HEADER_H + GAP;
static constexpr int CARD_W = (296 - GAP) / 2;
static constexpr int CARD_H = 176 - CARD_Y;

struct Card {
  lv_obj_t *title, *watts, *pv, *state, *today;
};

static const char* const NAMES[2] = {"driver", "passenger"};
static const char* const TITLES[2] = {"DRIVER", "PASSENGER"};
static lv_obj_t* total;
static Card cards[2];

// Small "label    value" line, 12px, as in the mockup
static lv_obj_t* line(lv_obj_t* card, const char* label) {
  lv_obj_t* r = lv_obj_create(card);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, lv_pct(100), 16);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* l = ui_text(r, label, &lv_font_montserrat_12, MUTED);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* v = ui_text(r, "--", &lv_font_montserrat_12, TEXT);
  lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
  return v;
}

static Card makeCard(lv_obj_t* page, int x, const char* title) {
  lv_obj_t* c = ui_card(page, x, CARD_Y, CARD_W, CARD_H);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(c, 4, 0);
  Card k;
  k.title = ui_text(c, title, &lv_font_montserrat_10, MUTED);
  lv_obj_set_style_text_letter_space(k.title, 1, 0);
  lv_obj_set_width(k.title, CARD_W - 2 * CARD_PAD);
  lv_label_set_long_mode(k.title, LV_LABEL_LONG_CLIP);
  k.watts = ui_text(c, "--", &lv_font_montserrat_24, SOLAR);
  lv_obj_set_style_pad_bottom(k.watts, 4, 0);
  k.pv = line(c, "PV");
  k.state = line(c, "State");
  k.today = line(c, "Today");
  return k;
}

void solar_screen_create(lv_obj_t* page) {
  lv_obj_t* header = lv_obj_create(page);
  lv_obj_remove_style_all(header);
  lv_obj_set_size(header, 296, HEADER_H);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* l = ui_text(header, "Total solar", &lv_font_montserrat_12, MUTED);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  total = ui_text(header, "--", &lv_font_montserrat_18, SOLAR);
  lv_obj_align(total, LV_ALIGN_RIGHT_MID, 0, 0);

  cards[0] = makeCard(page, 0, TITLES[0]);
  cards[1] = makeCard(page, CARD_W + GAP, TITLES[1]);
}

static void showCard(Card& k, const MpptData* m, int slot) {
  char buf[24];
  if (!m) {
    ui_set_text(k.title, TITLES[slot]);
    ui_set_text(k.watts, "--");
    ui_set_text(k.pv, "--");
    ui_set_text(k.state, "--");
    ui_set_text(k.today, "--");
    return;
  }
  // Matched by name: the fixed title. Otherwise the charger's own name.
  if (mppt_name_matches(m, NAMES[slot]) || !m->name[0]) ui_set_text(k.title, TITLES[slot]);
  else {
    size_t i = 0;
    for (; m->name[i] && i < sizeof(buf) - 1; i++) buf[i] = toupper((unsigned char)m->name[i]);
    buf[i] = 0;
    ui_set_text(k.title, buf);
  }
  ui_fmt_watts(buf, sizeof(buf), m->power);
  ui_set_text(k.watts, buf);
  ui_set_color(k.watts, SOLAR);
  ui_set_color(k.pv, TEXT);
  ui_set_color(k.today, TEXT);
  if (isnan(m->pvVolts)) ui_set_text(k.pv, "--");
  else {
    snprintf(buf, sizeof(buf), "%.1f V", m->pvVolts);
    ui_set_text(k.pv, buf);
  }
  ui_set_text(k.state, mppt_state_name(m->state));
  ui_set_color(k.state, m->state == 2 ? ALARM : TEXT);  // Fault
  if (isnan(m->yieldTodayKwh)) ui_set_text(k.today, "--");
  else {
    snprintf(buf, sizeof(buf), "%.1f kWh", m->yieldTodayKwh);
    ui_set_text(k.today, buf);
  }
}

void solar_screen_update(const VictronData& d) {
  char buf[24];
  ui_fmt_watts(buf, sizeof(buf), d.pvWatts);
  ui_set_text(total, buf);
  ui_set_color(total, SOLAR);

  const MpptData* placed[2] = {mppt_by_name(d, NAMES[0]), mppt_by_name(d, NAMES[1])};
  if (placed[0] == placed[1]) placed[1] = nullptr;  // one name containing both words
  // Fill an empty card with the next charger not already shown
  for (int slot = 0; slot < 2; slot++) {
    if (placed[slot]) continue;
    for (const MpptData& m : d.mppt) {
      if (m.instance < 0 || &m == placed[0] || &m == placed[1]) continue;
      if (mppt_name_matches(&m, NAMES[0]) || mppt_name_matches(&m, NAMES[1])) continue;
      placed[slot] = &m;
      break;
    }
  }
  for (int slot = 0; slot < 2; slot++) showCard(cards[slot], placed[slot], slot);
}
