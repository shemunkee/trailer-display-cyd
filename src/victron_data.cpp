#include "victron_data.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

static VictronData data;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;  // short copies only

// ---------- payload parsing ----------

// The payload is {"value": X}. X can be a number, a string, null, or (rarely)
// a list; an empty payload means the device has gone (SPEC 5.4).
struct Value {
  bool present = false;  // false: null, empty or unparsable
  float num = NAN;
  char str[24] = "";
  bool isString = false;
};

static Value parse(const uint8_t* payload, unsigned len) {
  Value v;
  char buf[96];
  if (len == 0 || len >= sizeof(buf)) return v;
  memcpy(buf, payload, len);
  buf[len] = 0;
  const char* p = strstr(buf, "\"value\"");
  if (!p) return v;
  p = strchr(p + 7, ':');
  if (!p) return v;
  p++;
  while (*p == ' ' || *p == '\t') p++;
  if (*p == '"') {
    p++;
    size_t n = 0;
    while (*p && *p != '"' && n < sizeof(v.str) - 1) v.str[n++] = *p++;
    v.str[n] = 0;
    v.isString = v.present = true;
    return v;
  }
  if (*p == 'n') return v;  // null
  char* end;
  float f = strtof(p, &end);
  if (end != p) {
    v.num = f;
    v.present = true;
  }
  return v;
}

// ---------- path routing ----------

// "solarcharger/279/Yield/Power" -> service "solarcharger", instance 279, rest "Yield/Power"
static bool split(const char* path, char* service, size_t serviceLen, int& instance, const char*& rest) {
  const char* a = strchr(path, '/');
  if (!a || (size_t)(a - path) >= serviceLen) return false;
  memcpy(service, path, a - path);
  service[a - path] = 0;
  char* end;
  instance = strtol(a + 1, &end, 10);
  if (end == a + 1 || *end != '/') return false;
  rest = end + 1;
  return true;
}

static MpptData* mpptSlot(int instance) {
  for (auto& m : data.mppt)
    if (m.instance == instance) return &m;
  for (auto& m : data.mppt)
    if (m.instance < 0) {
      m = MpptData();
      m.instance = instance;
      return &m;
    }
  return nullptr;  // more chargers than slots; ignore the extra ones
}

static void setNum(float& field, const Value& v) { field = v.present && !v.isString ? v.num : NAN; }
static void setCode(int& field, const Value& v) { field = v.present && !v.isString ? (int)v.num : -1; }

void victron_data_on_message(const char* path, const uint8_t* payload, unsigned len) {
  Value v = parse(payload, len);
  char service[16];
  int inst;
  const char* rest;
  if (!split(path, service, sizeof(service), inst, rest)) {
    portENTER_CRITICAL(&mux);
    data.lastMessageMs = millis() | 1;
    portEXIT_CRITICAL(&mux);
    return;
  }

  int group = -1;
  portENTER_CRITICAL(&mux);
  data.lastMessageMs = millis() | 1;
  if (!strcmp(service, "system")) {
    if (!strcmp(rest, "Dc/Battery/Soc")) setNum(data.soc, v), group = GROUP_BATTERY;
    else if (!strcmp(rest, "Dc/Battery/Voltage")) setNum(data.battVolts, v), group = GROUP_BATTERY;
    else if (!strcmp(rest, "Dc/Battery/Current")) setNum(data.battAmps, v), group = GROUP_BATTERY;
    else if (!strcmp(rest, "Dc/Battery/Power")) setNum(data.battWatts, v), group = GROUP_BATTERY;
    else if (!strcmp(rest, "Dc/Battery/TimeToGo")) setNum(data.timeToGoSec, v), group = GROUP_BATTERY;
    else if (!strcmp(rest, "Dc/Pv/Power")) setNum(data.pvWatts, v), group = GROUP_SOLAR;
    else if (!strcmp(rest, "Ac/ActiveIn/Source")) setCode(data.acSource, v), group = GROUP_AC;
    else if (!strcmp(rest, "Ac/Grid/L1/Power")) setNum(data.gridWatts, v), group = GROUP_AC;
    else if (!strcmp(rest, "Ac/Genset/L1/Power")) setNum(data.gensetWatts, v), group = GROUP_AC;
    else if (!strcmp(rest, "Ac/Consumption/L1/Power")) setNum(data.loadWatts, v), group = GROUP_AC;
  } else if (!strcmp(service, "battery")) {
    if (!strcmp(rest, "ConsumedAmphours")) setNum(data.consumedAh, v), group = GROUP_BATTERY;
  } else if (!strcmp(service, "solarcharger")) {
    MpptData* m = mpptSlot(inst);
    if (m) {
      group = GROUP_SOLAR;
      if (!strcmp(rest, "CustomName")) strlcpy(m->name, v.isString ? v.str : "", sizeof(m->name));
      else if (!strcmp(rest, "Yield/Power")) setNum(m->power, v);
      else if (!strcmp(rest, "Pv/V")) setNum(m->pvVolts, v);
      else if (!strcmp(rest, "History/Daily/0/Yield")) setNum(m->yieldTodayKwh, v);
      else if (!strcmp(rest, "State")) setCode(m->state, v);
      else if (!strcmp(rest, "ErrorCode")) setCode(m->errorCode, v);
      else group = -1;
    }
  } else if (!strcmp(service, "vebus")) {
    if (!strcmp(rest, "Ac/ActiveIn/L1/V")) setNum(data.acInVolts, v), group = GROUP_AC;
    else if (!strcmp(rest, "Ac/Out/L1/V")) setNum(data.acOutVolts, v), group = GROUP_AC;
    else if (!strcmp(rest, "Ac/Out/L1/I")) setNum(data.acOutAmps, v), group = GROUP_AC;
    else if (!strcmp(rest, "Ac/Out/L1/F")) setNum(data.acOutHz, v), group = GROUP_AC;
    else if (!strcmp(rest, "Ac/ActiveIn/CurrentLimit")) setNum(data.inputLimitAmps, v), group = GROUP_QUATTRO;
    else if (!strcmp(rest, "State")) setCode(data.vebusState, v), group = GROUP_QUATTRO;
  }
  // Alarms (vebus/battery Alarms/*) are read in checklist item 13
  if (group >= 0) data.updatedMs[group] = millis() | 1;  // never 0, which means "never"
  portEXIT_CRITICAL(&mux);
}

void victron_data_get(VictronData& out) {
  portENTER_CRITICAL(&mux);
  out = data;
  portEXIT_CRITICAL(&mux);
}

// ---------- names ----------

// vebus/<inst>/State codes, from the Venus OS dbus documentation
const char* vebus_state_name(int s) {
  switch (s) {
    case 0: return "Off";
    case 1: return "Low power";
    case 2: return "Fault";
    case 3: return "Bulk";
    case 4: return "Absorption";
    case 5: return "Float";
    case 6: return "Storage";
    case 7: return "Equalize";
    case 8: return "Passthru";
    case 9: return "Inverting";
    case 10: return "Assisting";
    case 11: return "Power supply";
    case 244: return "Sustain";
    case 252: return "External";
    default: return "--";
  }
}

bool vebus_state_charging(int s) { return s >= 3 && s <= 7; }

// solarcharger/<inst>/State codes
const char* mppt_state_name(int s) {
  switch (s) {
    case 0: return "Off";
    case 2: return "Fault";
    case 3: return "Bulk";
    case 4: return "Absorption";
    case 5: return "Float";
    case 6: return "Storage";
    case 7: return "Equalize";
    case 245: return "Starting";
    case 252: return "External";
    default: return "--";
  }
}

bool mppt_name_matches(const MpptData* m, const char* name) {
  return m && m->instance >= 0 && m->name[0] && strcasestr(m->name, name);
}

const MpptData* mppt_by_name(const VictronData& d, const char* name) {
  for (const MpptData& m : d.mppt)
    if (mppt_name_matches(&m, name)) return &m;
  return nullptr;
}
