#include "setup_screen.h"

#include <Arduino.h>
#include <lvgl.h>

#include "board.h"
#include "theme.h"
#include "ui.h"

using namespace theme;

static constexpr int QR_SIZE = 100;
static constexpr int QR_BORDER = 4;  // white outline outside the code, adds to LVGL's own margin

static lv_obj_t* title;
static void (*cancelCb)() = nullptr;

static void qr(lv_obj_t* parent, const String& data, int y) {
  lv_obj_t* q = lv_qrcode_create(parent);
  lv_qrcode_set_size(q, QR_SIZE);
  lv_qrcode_set_dark_color(q, lv_color_black());
  lv_qrcode_set_light_color(q, lv_color_white());
  lv_qrcode_update(q, data.c_str(), data.length());
  lv_obj_set_style_outline_color(q, lv_color_white(), 0);
  lv_obj_set_style_outline_width(q, QR_BORDER, 0);
  lv_obj_align(q, LV_ALIGN_TOP_MID, 0, y);
}

// One card: step label, QR code, then a muted line and a bright value line
static void qrCard(lv_obj_t* scr, int x, int w, const char* step, const String& data,
                   const char* line1, const char* line2) {
  lv_obj_t* c = ui_card(scr, x, 44, w, SCREEN_H - PAD - 44);
  ui_text(c, step, &lv_font_montserrat_12, MUTED);
  qr(c, data, 20 + QR_BORDER);
  lv_obj_t* l1 = ui_text(c, line1, &lv_font_montserrat_12, MUTED);
  lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 20 + QR_SIZE + 2 * QR_BORDER + 6);
  lv_obj_t* l2 = ui_text(c, line2, &lv_font_montserrat_14, TEXT);
  lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, 20 + QR_SIZE + 2 * QR_BORDER + 22);
}

void setup_screen_create(const char* ssid, const char* pass, const char* url, void (*onCancel)()) {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(BG), 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  title = ui_text(scr, "Setup", &lv_font_montserrat_18, TEXT);
  lv_obj_set_pos(title, PAD, PAD + 3);

  cancelCb = onCancel;
  if (onCancel) {
    lv_obj_t* b = ui_button(scr, "Cancel", SCREEN_W - PAD - 72, PAD - 2, 72, 28);
    lv_obj_add_event_cb(b, [](lv_event_t*) { cancelCb(); }, LV_EVENT_CLICKED, nullptr);
  }

  // Standard WiFi QR format; the SSID and password alphabet need no escaping
  String wifi = String("WIFI:T:WPA;S:") + ssid + ";P:" + pass + ";;";
  String netLine = String("Network ") + ssid;
  String passLine = String("Pass ") + pass;
  int w = (SCREEN_W - 2 * PAD - GAP) / 2;
  qrCard(scr, PAD, w, "1  Join setup WiFi", wifi, netLine.c_str(), passLine.c_str());
  qrCard(scr, PAD + w + GAP, w, "2  Open the page", url, "In a browser", url + 7);  // drop "http://"
}

void setup_screen_status(const char* msg) {
  lv_label_set_text(title, msg);
}
