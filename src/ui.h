// Small LVGL helpers shared by the screens, styled per SPEC.md 6.1.
#pragma once

#include <lvgl.h>
#include <stddef.h>

lv_obj_t* ui_card(lv_obj_t* parent, int x, int y, int w, int h);
lv_obj_t* ui_text(lv_obj_t* parent, const char* s, const lv_font_t* font, uint32_t color);
lv_obj_t* ui_button(lv_obj_t* parent, const char* label, int x, int y, int w, int h);

// Stale data (SPEC 5.6): while dimmed, every color set through ui_color() or
// ui_set_color() comes out muted, so the last known values show greyed
void ui_set_dim(bool dim);
uint32_t ui_color(uint32_t color);

// Only touch a label when its text or color changes, so the panel isn't redrawn every tick
void ui_set_text(lv_obj_t* label, const char* s);
void ui_set_color(lv_obj_t* label, uint32_t color);

// 640 W, 2.1 kW; "--" when unknown (NAN). sign: show + for positive values
void ui_fmt_watts(char* buf, size_t len, float w, bool sign = false);

// A "label ........ value" row that fills its share of a column (flex grow 1),
// with a divider below unless last. Returns the value label.
lv_obj_t* ui_row(lv_obj_t* column, const char* label, bool last = false);
// A column for ui_row()s that fills the rest of its parent
lv_obj_t* ui_rows(lv_obj_t* parent);
