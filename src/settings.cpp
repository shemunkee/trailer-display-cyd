#include "settings.h"

#include <Arduino.h>
#include <Preferences.h>

static constexpr const char* NVS_NS = "settings";

static constexpr uint8_t DEF_LOW_SOC = 30;
static constexpr uint8_t DEF_SNOOZE = 30;
static constexpr uint8_t DEF_BRIGHTNESS = 70;
static const uint8_t SNOOZE_OPTIONS[] = {5, 10, 30, 60, 90};
static constexpr int SNOOZE_COUNT = sizeof(SNOOZE_OPTIONS) / sizeof(SNOOZE_OPTIONS[0]);

static Settings cur = {DEF_LOW_SOC, DEF_SNOOZE, DEF_BRIGHTNESS, DataSource::Auto, AcSource::None, false};

static uint8_t snapStep(int v, int step, int lo, int hi) {
  v = ((v + step / 2) / step) * step;
  return constrain(v, lo, hi);
}

static uint8_t snapSnooze(int sec) {
  int best = 0;
  for (int i = 1; i < SNOOZE_COUNT; i++)
    if (abs(SNOOZE_OPTIONS[i] - sec) < abs(SNOOZE_OPTIONS[best] - sec)) best = i;
  return SNOOZE_OPTIONS[best];
}

static bool validSnooze(int sec) {
  for (int i = 0; i < SNOOZE_COUNT; i++)
    if (SNOOZE_OPTIONS[i] == sec) return true;
  return false;
}

static void write(const char* key, uint8_t v) {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.putUChar(key, v);
  prefs.end();
}

void settings_load() {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  int lowSoc = prefs.getUChar("lowsoc", DEF_LOW_SOC);
  int snooze = prefs.getUChar("snooze", DEF_SNOOZE);
  int bright = prefs.getUChar("bright", DEF_BRIGHTNESS);
  int lastAc = prefs.getUChar("lastac", (uint8_t)AcSource::None);
  int flip = prefs.getUChar("flip", 0);
  prefs.end();

  // Anything out of range (corrupt or from an older layout) goes back to its default
  cur.lowSocPct = (lowSoc >= 5 && lowSoc <= 95 && lowSoc % 5 == 0) ? lowSoc : DEF_LOW_SOC;
  cur.snoozeSec = validSnooze(snooze) ? snooze : DEF_SNOOZE;
  cur.brightnessPct = (bright >= 10 && bright <= 100 && bright % 10 == 0) ? bright : DEF_BRIGHTNESS;
  cur.source = DataSource::Auto;  // only option in v1, so "src" isn't read back yet
  cur.lastAc = lastAc <= (int)AcSource::Generator ? (AcSource)lastAc : AcSource::None;
  cur.flipped = flip == 1;

  static const char* acNames[] = {"none", "shore", "generator"};
  Serial.printf("Settings: low SOC %d%%, snooze %ds, brightness %d%%, source auto, last AC %s, USB %s\n",
                cur.lowSocPct, cur.snoozeSec, cur.brightnessPct, acNames[(int)cur.lastAc],
                cur.flipped ? "left" : "right");
}

const Settings& settings_get() { return cur; }

void settings_set_low_soc(int pct) {
  uint8_t v = snapStep(pct, 5, 5, 95);
  if (v == cur.lowSocPct) return;
  cur.lowSocPct = v;
  write("lowsoc", v);
}

void settings_set_snooze(int sec) {
  uint8_t v = snapSnooze(sec);
  if (v == cur.snoozeSec) return;
  cur.snoozeSec = v;
  write("snooze", v);
}

void settings_set_brightness(int pct) {
  uint8_t v = snapStep(pct, 10, 10, 100);
  if (v == cur.brightnessPct) return;
  cur.brightnessPct = v;
  write("bright", v);
}

void settings_set_source(DataSource s) {
  if (s == cur.source) return;
  cur.source = s;
  write("src", (uint8_t)s);
}

void settings_set_last_ac(AcSource s) {
  if (s == cur.lastAc) return;
  cur.lastAc = s;
  write("lastac", (uint8_t)s);
}

void settings_set_flipped(bool f) {
  if (f == cur.flipped) return;
  cur.flipped = f;
  write("flip", f ? 1 : 0);
}

int settings_snooze_step(int sec, int dir) {
  int i = 0;
  while (i < SNOOZE_COUNT - 1 && SNOOZE_OPTIONS[i] != snapSnooze(sec)) i++;
  i = constrain(i + (dir > 0 ? 1 : -1), 0, SNOOZE_COUNT - 1);
  return SNOOZE_OPTIONS[i];
}
