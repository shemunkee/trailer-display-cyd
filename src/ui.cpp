#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "theme.h"

using namespace theme;

lv_obj_t* ui_card(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* c = lv_obj_create(parent);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_size(c, w, h);
  lv_obj_set_style_bg_color(c, lv_color_hex(CARD), 0);
  lv_obj_set_style_radius(c, RADIUS, 0);
  lv_obj_set_style_border_width(c, 0, 0);
  lv_obj_set_style_pad_all(c, CARD_PAD, 0);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

lv_obj_t* ui_text(lv_obj_t* parent, const char* s, const lv_font_t* font, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, s);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  return l;
}

lv_obj_t* ui_button(lv_obj_t* parent, const char* label, int x, int y, int w, int h) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, lv_color_hex(DIVIDER), 0);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_ext_click_area(b, 4);
  lv_obj_t* l = ui_text(b, label, &lv_font_montserrat_12, TEXT);
  lv_obj_center(l);
  return b;
}

void ui_set_text(lv_obj_t* l, const char* s) {
  if (strcmp(lv_label_get_text(l), s) != 0) lv_label_set_text(l, s);
}

void ui_set_color(lv_obj_t* l, uint32_t c) {
  lv_color_t col = lv_color_hex(c);
  if (!lv_color_eq(lv_obj_get_style_text_color(l, 0), col)) lv_obj_set_style_text_color(l, col, 0);
}

void ui_fmt_watts(char* buf, size_t len, float w, bool sign) {
  if (isnan(w)) snprintf(buf, len, "--");
  else if (fabsf(w) < 999.5f) snprintf(buf, len, sign ? "%+.0f W" : "%.0f W", w);
  else snprintf(buf, len, sign ? "%+.1f kW" : "%.1f kW", w / 1000.0f);
}

lv_obj_t* ui_rows(lv_obj_t* parent) {
  lv_obj_t* c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_set_width(c, lv_pct(100));
  lv_obj_set_flex_grow(c, 1);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  return c;
}

lv_obj_t* ui_row(lv_obj_t* column, const char* label, bool last) {
  lv_obj_t* r = lv_obj_create(column);
  lv_obj_remove_style_all(r);
  lv_obj_set_width(r, lv_pct(100));
  lv_obj_set_flex_grow(r, 1);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  if (!last) {
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_border_color(r, lv_color_hex(DIVIDER), 0);
  }
  lv_obj_t* l = ui_text(r, label, &lv_font_montserrat_12, MUTED);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* v = ui_text(r, "--", &lv_font_montserrat_14, TEXT);
  lv_obj_align(v, LV_ALIGN_RIGHT_MID, 0, 0);
  return v;
}
