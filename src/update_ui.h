// Update prompts on the screen (SPEC.md 5.7): a blue UPDATE label top left on
// the data screens when a new version is ready, an "Install?" dialog, and a
// progress screen while it installs. Nothing installs without a tap.
#pragma once

#include <lvgl.h>

void update_ui_create(lv_obj_t* scr);  // after the pages, so the label sits on top
void update_ui_tick(bool dataPage);    // from the nav update timer
void update_ui_confirm();              // open the "Install vX?" dialog
