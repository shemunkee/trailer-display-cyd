// User settings (SPEC.md 6.8 and 6.3), stored in NVS namespace "settings".
// Setters snap to a valid value and write to NVS only when it changes.
#pragma once

#include <stdint.h>

enum class DataSource : uint8_t { Auto = 0 };
enum class AcSource : uint8_t { None = 0, Shore = 1, Generator = 2 };

struct Settings {
  uint8_t lowSocPct;      // 5-95, steps of 5
  uint8_t snoozeSec;      // 5, 10, 30, 60, 90
  uint8_t brightnessPct;  // 10-100, steps of 10
  DataSource source;
  AcSource lastAc;        // last live AC input, kept across reboots
  bool flipped;           // screen turned 180 degrees: USB on the left instead of the right
};

void settings_load();
const Settings& settings_get();

void settings_set_low_soc(int pct);
void settings_set_snooze(int sec);
void settings_set_brightness(int pct);
void settings_set_source(DataSource s);
void settings_set_last_ac(AcSource s);
void settings_set_flipped(bool f);

int settings_snooze_step(int sec, int dir);  // next snooze option up (dir > 0) or down
