#include "alarms_screen.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "nav.h"
#include "settings.h"
#include "theme.h"
#include "ui.h"

using namespace theme;

// Content area is 296 x 176 inside the page padding
static constexpr int SHOWN = 2;
static constexpr int CARD_H = 64;
static constexpr int PAGER_Y = SHOWN * (CARD_H + GAP);
static constexpr int FOOTER_Y = 176 - 12;

struct Card {
  lv_obj_t *card, *stripe, *level, *msg, *meta;
};

struct Item {
  uint8_t level;
  uint32_t sinceMs;
  const AlarmEntry* entry;  // nullptr = our own low SOC warning
};

static Card cards[SHOWN];
static lv_obj_t *emptyLabel, *pager;
static int pageIndex = 0;
static int itemCount = 0;
static uint32_t lowSocSince = 0;  // millis() when SOC fell to the low SOC setting, 0 = not low

static Card makeCard(lv_obj_t* page, int y) {
  Card c;
  c.card = ui_card(page, 0, y, 296, CARD_H);
  c.stripe = lv_obj_create(c.card);
  lv_obj_remove_style_all(c.stripe);
  lv_obj_set_size(c.stripe, 4, CARD_H - 2 * CARD_PAD);
  lv_obj_set_style_radius(c.stripe, 2, 0);
  lv_obj_set_style_bg_opa(c.stripe, LV_OPA_COVER, 0);
  lv_obj_set_pos(c.stripe, 0, 0);

  lv_obj_t* col = lv_obj_create(c.card);
  lv_obj_remove_style_all(col);
  lv_obj_set_pos(col, 12, 0);
  lv_obj_set_size(col, 296 - 2 * CARD_PAD - 12, CARD_H - 2 * CARD_PAD);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(col, 2, 0);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  c.level = ui_text(col, "", &lv_font_montserrat_10, ALARM);
  lv_obj_set_style_text_letter_space(c.level, 1, 0);
  c.msg = ui_text(col, "", &lv_font_montserrat_14, TEXT);
  c.meta = ui_text(col, "", &lv_font_montserrat_10, MUTED);
  lv_obj_t* labels[] = {c.msg, c.meta};
  for (lv_obj_t* l : labels) {
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  }
  lv_obj_add_flag(c.card, LV_OBJ_FLAG_HIDDEN);
  return c;
}

static void onPager(lv_event_t*) {
  int pages = (itemCount + SHOWN - 1) / SHOWN;
  if (pages > 1) pageIndex = (pageIndex + 1) % pages;
  nav_refresh();  // show the next cards now, not up to 500 ms later
}

void alarms_screen_create(lv_obj_t* page) {
  for (int i = 0; i < SHOWN; i++) cards[i] = makeCard(page, i * (CARD_H + GAP));

  emptyLabel = ui_text(page, "No active alarms", &lv_font_montserrat_14, MUTED);
  lv_obj_align(emptyLabel, LV_ALIGN_CENTER, 0, -10);

  pager = ui_text(page, "", &lv_font_montserrat_12, AC);
  lv_obj_set_width(pager, 296);
  lv_obj_set_style_text_align(pager, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(pager, 0, PAGER_Y);
  lv_obj_add_flag(pager, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(pager, 8);
  lv_obj_add_event_cb(pager, onPager, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* footer = ui_text(page, "Read only. Clear alarms on the Cerbo.", &lv_font_montserrat_10, MUTED);
  lv_obj_set_width(footer, 296);
  lv_obj_set_style_text_align(footer, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(footer, 0, FOOTER_Y);
}

// "just now", "for 12m", "for 3h 05m", "for 2d 4h"
static void fmtFor(char* buf, size_t len, uint32_t sinceMs) {
  uint32_t mins = (millis() - sinceMs) / 60000;
  if (mins < 1) snprintf(buf, len, "just now");
  else if (mins < 60) snprintf(buf, len, "for %lum", (unsigned long)mins);
  else if (mins < 24 * 60) snprintf(buf, len, "for %luh %02lum", (unsigned long)(mins / 60), (unsigned long)(mins % 60));
  else snprintf(buf, len, "for %lud %luh", (unsigned long)(mins / 1440), (unsigned long)(mins % 1440 / 60));
}

static void showCard(Card& c, const VictronData& d, const Item& it) {
  char msg[40], dev[28], since[20], meta[56];
  uint32_t color = it.level >= 2 ? ALARM : WARN;
  lv_color_t stripe = lv_color_hex(ui_color(color));
  if (!lv_color_eq(lv_obj_get_style_bg_color(c.stripe, 0), stripe)) lv_obj_set_style_bg_color(c.stripe, stripe, 0);
  ui_set_text(c.level, it.level >= 2 ? "ALARM" : "WARNING");
  ui_set_color(c.level, color);
  if (it.entry) {
    alarm_text(d, *it.entry, msg, sizeof(msg), dev, sizeof(dev));
  } else {
    snprintf(msg, sizeof(msg), "SOC at or below %d%%", settings_get().lowSocPct);
    strlcpy(dev, "Low SOC setting", sizeof(dev));
  }
  fmtFor(since, sizeof(since), it.sinceMs);
  snprintf(meta, sizeof(meta), "%s " LV_SYMBOL_BULLET " %s", dev, since);
  ui_set_text(c.msg, msg);
  ui_set_color(c.msg, TEXT);
  ui_set_text(c.meta, meta);
  lv_obj_remove_flag(c.card, LV_OBJ_FLAG_HIDDEN);
}

int alarms_screen_update(const VictronData& d) {
  bool low = !isnan(d.soc) && d.soc <= settings_get().lowSocPct;
  if (low && !lowSocSince) lowSocSince = millis() | 1;
  if (!low) lowSocSince = 0;

  // Alarms before warnings, newest first
  Item items[MAX_ALARMS + 1];
  int n = 0;
  for (const AlarmEntry& a : d.alarms)
    if (a.used) items[n++] = {a.level, a.sinceMs, &a};
  if (lowSocSince) items[n++] = {1, lowSocSince, nullptr};
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0; j--) {
      const Item &x = items[j - 1], &y = items[j];
      bool swap = y.level > x.level || (y.level == x.level && (int32_t)(y.sinceMs - x.sinceMs) > 0);
      if (!swap) break;
      Item t = items[j - 1];
      items[j - 1] = items[j];
      items[j] = t;
    }

  itemCount = n;
  int pages = (n + SHOWN - 1) / SHOWN;
  if (pageIndex >= pages) pageIndex = 0;
  for (int i = 0; i < SHOWN; i++) {
    int k = pageIndex * SHOWN + i;
    if (k < n) showCard(cards[i], d, items[k]);
    else lv_obj_add_flag(cards[i].card, LV_OBJ_FLAG_HIDDEN);
  }

  if (n) lv_obj_add_flag(emptyLabel, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(emptyLabel, LV_OBJ_FLAG_HIDDEN);

  if (pages > 1) {
    char buf[40];
    int first = pageIndex * SHOWN + 1, last = min(first + SHOWN - 1, n);
    snprintf(buf, sizeof(buf), "%d-%d of %d " LV_SYMBOL_BULLET " tap for more", first, last, n);
    ui_set_text(pager, buf);
    lv_obj_remove_flag(pager, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(pager, LV_OBJ_FLAG_HIDDEN);
  }
  return n;
}
