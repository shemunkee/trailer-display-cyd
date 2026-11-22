#include "snooze.h"

#include <Arduino.h>
#include <lvgl.h>

#include "display.h"
#include "board.h"
#include "logo_a8.h"
#include "settings.h"
#include "theme.h"

static constexpr uint32_t LOGO_MS = 1000;
static constexpr uint32_t CHECK_MS = 100;

enum class State { Awake, Logo, Asleep };

static State state = State::Awake;
static uint32_t logoShownAt = 0;
static lv_image_dsc_t logoDsc;
static lv_obj_t* cover;  // full-screen black cover with the logo, on the top layer; also catches the wake tap

static void lightUp() {
  display_set_brightness(settings_get().brightnessPct);
  lv_display_trigger_activity(nullptr);
  state = State::Awake;
}

bool snooze_asleep() { return state == State::Asleep; }

void snooze_wake() {
  if (state == State::Awake) return;
  lv_obj_add_flag(cover, LV_OBJ_FLAG_HIDDEN);
  lightUp();
}

// The waking tap: light up on press, but keep the cover until the finger lifts,
// otherwise LVGL would hand the same press to whatever is underneath
static void onCover(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_PRESSED) lightUp();
  else lv_obj_add_flag(cover, LV_OBJ_FLAG_HIDDEN);  // RELEASED or PRESS_LOST
}

static void tick(lv_timer_t*) {
  switch (state) {
    case State::Awake:
      if (lv_display_get_inactive_time(nullptr) >= settings_get().snoozeSec * 1000UL) {
        lv_obj_move_foreground(cover);  // above anything else on the top layer, like the Data source menu
        lv_obj_remove_flag(cover, LV_OBJ_FLAG_HIDDEN);
        logoShownAt = millis();
        state = State::Logo;
      }
      break;
    case State::Logo:
      if (millis() - logoShownAt >= LOGO_MS) {
        display_set_brightness(0);
        state = State::Asleep;
      }
      break;
    case State::Asleep:
      break;
  }
}

void snooze_begin() {
  // The logo is an alpha mask in flash, tinted solar pink (like the nav icons)
  logoDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  logoDsc.header.cf = LV_COLOR_FORMAT_A8;
  logoDsc.header.w = LOGO_W;
  logoDsc.header.h = LOGO_H;
  logoDsc.header.stride = LOGO_W;
  logoDsc.data_size = sizeof(logo_a8);
  logoDsc.data = logo_a8;

  cover = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(cover);
  lv_obj_set_size(cover, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(cover, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, 0);
  lv_obj_remove_flag(cover, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* logo = lv_image_create(cover);  // not clickable, so taps reach the cover
  lv_image_set_src(logo, &logoDsc);
  lv_obj_set_style_image_recolor(logo, lv_color_hex(theme::SOLAR), 0);
  lv_obj_set_style_image_recolor_opa(logo, LV_OPA_COVER, 0);
  lv_obj_set_pos(logo, LOGO_X, LOGO_Y);
  lv_obj_add_flag(cover, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(cover, LV_OBJ_FLAG_PRESS_LOCK);  // the press stays on the cover even if the finger slides
  lv_obj_add_flag(cover, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(cover, onCover, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(cover, onCover, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(cover, onCover, LV_EVENT_PRESS_LOST, nullptr);

  lv_timer_create(tick, CHECK_MS, nullptr);
}
