#include "update_ui.h"

#include <stdio.h>

#include "board.h"
#include "ota.h"
#include "snooze.h"
#include "theme.h"
#include "ui.h"

using namespace theme;

static lv_obj_t* marker;    // UPDATE, top left
static lv_obj_t* dialog;    // Install? (top layer while open)
static lv_obj_t* progress;  // full-screen progress while installing (top layer)
static lv_obj_t *progressBar, *progressText;

// A full-screen backdrop on the top layer that swallows taps
static lv_obj_t* backdrop(lv_opa_t opa) {
  lv_obj_t* b = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, SCREEN_W, SCREEN_H);
  lv_obj_set_style_bg_color(b, lv_color_hex(BG), 0);
  lv_obj_set_style_bg_opa(b, opa, 0);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  return b;
}

static void closeDialog() {
  if (dialog) lv_obj_delete(dialog);
  dialog = nullptr;
}

static void onInstall(lv_event_t*) {
  closeDialog();
  ota_request_install();
}

static void onLater(lv_event_t*) { closeDialog(); }

static lv_obj_t* dialogButton(lv_obj_t* parent, const char* text, bool main, lv_event_cb_t cb) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, 96, 34);
  lv_obj_set_style_bg_color(b, lv_color_hex(main ? AC : 0x1A1D24), 0);
  lv_obj_set_style_border_width(b, main ? 0 : 1, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(DIVIDER), 0);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t* l = ui_text(b, text, &lv_font_montserrat_14, main ? BG : TEXT);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

void update_ui_confirm() {
  OtaStatus st = ota_status();
  if (dialog || (st.state != OtaState::Available && st.state != OtaState::InstallFailed)) return;
  dialog = backdrop(LV_OPA_70);

  lv_obj_t* card = lv_obj_create(dialog);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, 240, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(CARD), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(DIVIDER), 0);
  lv_obj_set_style_radius(card, RADIUS, 0);
  lv_obj_set_style_pad_all(card, PAD, 0);
  lv_obj_set_style_pad_row(card, GAP, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(card);

  char buf[48];
  snprintf(buf, sizeof(buf), "Update to %s?", st.latest);
  ui_text(card, buf, &lv_font_montserrat_18, TEXT);
  snprintf(buf, sizeof(buf), "You have %s.", ota_version());
  ui_text(card, buf, &lv_font_montserrat_12, MUTED);
  lv_obj_t* note = ui_text(card, "Takes about a minute. The display restarts when it's done.", &lv_font_montserrat_12, MUTED);
  lv_obj_set_width(note, lv_pct(100));
  lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);

  lv_obj_t* buttons = lv_obj_create(card);
  lv_obj_remove_style_all(buttons);
  lv_obj_set_size(buttons, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(buttons, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(buttons, LV_OBJ_FLAG_CLICKABLE);
  dialogButton(buttons, "Later", false, onLater);
  dialogButton(buttons, "Install", true, onInstall);
}

static void onMarker(lv_event_t*) { update_ui_confirm(); }

void update_ui_create(lv_obj_t* scr) {
  // In the top padding like the STALE marker, but on the left
  marker = ui_text(scr, "UPDATE", &lv_font_montserrat_10, AC);
  lv_obj_set_style_text_letter_space(marker, 1, 0);
  lv_obj_align(marker, LV_ALIGN_TOP_LEFT, PAD, 0);
  lv_obj_add_flag(marker, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(marker, 10);
  lv_obj_add_event_cb(marker, onMarker, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
}

static void showProgress(const OtaStatus& st) {
  if (!progress) {
    closeDialog();
    progress = backdrop(LV_OPA_COVER);
    char buf[40];
    snprintf(buf, sizeof(buf), "Updating to %s", st.latest);
    lv_obj_t* t = ui_text(progress, buf, &lv_font_montserrat_18, TEXT);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -30);
    progressBar = lv_bar_create(progress);
    lv_obj_set_size(progressBar, 220, 8);
    lv_bar_set_range(progressBar, 0, 100);
    lv_obj_set_style_radius(progressBar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(progressBar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(progressBar, lv_color_hex(DIVIDER), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(progressBar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(progressBar, lv_color_hex(AC), LV_PART_INDICATOR);
    lv_obj_align(progressBar, LV_ALIGN_CENTER, 0, 0);
    progressText = ui_text(progress, "", &lv_font_montserrat_12, MUTED);
    lv_obj_align(progressText, LV_ALIGN_CENTER, 0, 22);
  }
  if (lv_bar_get_value(progressBar) != st.progress) lv_bar_set_value(progressBar, st.progress, LV_ANIM_OFF);
  char buf[48];
  if (st.state == OtaState::Restarting) snprintf(buf, sizeof(buf), "Done, restarting");
  else snprintf(buf, sizeof(buf), "%u%%  " LV_SYMBOL_BULLET "  Keep it plugged in", st.progress);
  ui_set_text(progressText, buf);
  // Stay awake and in front while it installs
  snooze_wake();
  lv_display_trigger_activity(nullptr);
  lv_obj_move_foreground(progress);
}

void update_ui_tick(bool dataPage) {
  OtaStatus st = ota_status();
  bool installing = st.state == OtaState::Installing || st.state == OtaState::Restarting;
  if (installing) showProgress(st);
  else if (progress) {  // failed: back to the screens; the reason shows in Settings
    lv_obj_delete(progress);
    progress = nullptr;
  }

  bool ready = st.state == OtaState::Available || st.state == OtaState::InstallFailed;
  if (ready && dataPage && !installing) lv_obj_remove_flag(marker, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(marker, LV_OBJ_FLAG_HIDDEN);
}
