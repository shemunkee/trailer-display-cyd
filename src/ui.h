// Small LVGL helpers shared by the screens, styled per SPEC.md 6.1.
#pragma once

#include <lvgl.h>

lv_obj_t* ui_card(lv_obj_t* parent, int x, int y, int w, int h);
lv_obj_t* ui_text(lv_obj_t* parent, const char* s, const lv_font_t* font, uint32_t color);
lv_obj_t* ui_button(lv_obj_t* parent, const char* label, int x, int y, int w, int h);
