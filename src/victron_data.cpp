#include "victron_data.h"

#include <Arduino.h>
#include <ctype.h>
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

static int levelOf(const Value& v) { return v.present && !v.isString && !isnan(v.num) ? (int)v.num : 0; }

// level 0 clears; anything else adds or updates (SPEC 6.7)
static void setAlarm(AlarmDevice dev, int inst, const char* name, int variant, int level, int code) {
  AlarmEntry* e = nullptr;
  for (auto& a : data.alarms)
    if (a.used && a.device == dev && a.instance == inst && !strcmp(a.name, name)) e = &a;
  if (level <= 0) {
    if (!e) return;
    e->variantLevel[variant] = 0;
    e->level = max(e->variantLevel[0], e->variantLevel[1]);
    if (!e->level) e->used = false;
    return;
  }
  if (level > 2) level = 2;
  if (!e) {
    for (auto& a : data.alarms)
      if (!a.used) {
        e = &a;
        break;
      }
    if (!e) return;  // table full; the dot still shows the others
    *e = AlarmEntry();
    e->used = true;
    e->device = dev;
    e->instance = inst;
    strlcpy(e->name, name, sizeof(e->name));
    e->sinceMs = millis();
  }
  uint8_t before = e->level;
  e->variantLevel[variant] = level;
  e->level = max(e->variantLevel[0], e->variantLevel[1]);
  e->code = code;
  if (e->level > before) e->serial = ++data.alarmSerial;  // new, or a warning became an alarm
}

static void setNum(float& field, const Value& v) { field = v.present && !v.isString ? v.num : NAN; }
// Solar power reads a few watts below zero at night (seen: -5.7 W); show 0
static void setPower(float& field, const Value& v) {
  setNum(field, v);
  if (field < 0) field = 0;
}
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
    else if (!strcmp(rest, "Dc/Pv/Power")) setPower(data.pvWatts, v), group = GROUP_SOLAR;
    else if (!strcmp(rest, "Ac/ActiveIn/Source")) setCode(data.acSource, v), group = GROUP_AC;
    else if (!strcmp(rest, "Ac/Grid/L1/Power")) setNum(data.gridWatts, v), group = GROUP_AC;
    else if (!strcmp(rest, "Ac/Genset/L1/Power")) setNum(data.gensetWatts, v), group = GROUP_AC;
    else if (!strcmp(rest, "Ac/Consumption/L1/Power")) setNum(data.loadWatts, v), group = GROUP_AC;
  } else if (!strcmp(service, "battery")) {
    if (!strcmp(rest, "ConsumedAmphours")) setNum(data.consumedAh, v), group = GROUP_BATTERY;
    else if (!strncmp(rest, "Alarms/", 7)) setAlarm(AlarmDevice::Battery, inst, rest + 7, 0, levelOf(v), 0);
  } else if (!strcmp(service, "solarcharger")) {
    MpptData* m = mpptSlot(inst);
    if (m) {
      group = GROUP_SOLAR;
      if (!strcmp(rest, "CustomName")) strlcpy(m->name, v.isString ? v.str : "", sizeof(m->name));
      else if (!strcmp(rest, "Yield/Power")) setPower(m->power, v);
      else if (!strcmp(rest, "Pv/V")) setNum(m->pvVolts, v);
      else if (!strcmp(rest, "History/Daily/0/Yield")) setNum(m->yieldTodayKwh, v);
      else if (!strcmp(rest, "State")) setCode(m->state, v);
      else if (!strcmp(rest, "ErrorCode")) {
        setCode(m->errorCode, v);
        setAlarm(AlarmDevice::Mppt, inst, "ErrorCode", 0, m->errorCode > 0 ? 2 : 0, m->errorCode);
      }
      else group = -1;
    }
  } else if (!strcmp(service, "vebus")) {
    if (!strcmp(rest, "Ac/ActiveIn/L1/V")) setNum(data.acInVolts, v), group = GROUP_AC;
    else if (!strcmp(rest, "Ac/Out/L1/V")) setNum(data.acOutVolts, v), group = GROUP_AC;
    else if (!strcmp(rest, "Ac/Out/L1/I")) setNum(data.acOutAmps, v), group = GROUP_AC;
    else if (!strcmp(rest, "Ac/Out/L1/F")) setNum(data.acOutHz, v), group = GROUP_AC;
    else if (!strcmp(rest, "Ac/ActiveIn/CurrentLimit")) setNum(data.inputLimitAmps, v), group = GROUP_QUATTRO;
    else if (!strcmp(rest, "State")) setCode(data.vebusState, v), group = GROUP_QUATTRO;
    else if (!strncmp(rest, "Alarms/", 7)) {
      const char* name = rest + 7;
      int variant = 0;
      if (!strncmp(name, "L1/", 3)) name += 3, variant = 1;
      setAlarm(AlarmDevice::Quattro, inst, name, variant, levelOf(v), 0);
    }
  }
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

// ---------- alarm text ----------

struct NameText {
  const char* name;
  const char* text;
};

// Names whose split-up words would read badly; the rest are split automatically
static const NameText ALARM_NAMES[] = {
    {"LowSoc", "Low state of charge"},
    {"Ripple", "High DC ripple"},
    {"GridLost", "AC input lost"},
    {"MidVoltage", "Midpoint voltage"},
    {"BmsConnectionLost", "BMS connection lost"},
    {"BmsPreAlarm", "BMS pre-alarm"},
    {"HighDcCurrent", "High DC current"},
    {"HighDcVoltage", "High DC voltage"},
    {"Alarm", "Battery alarm"},
};

// solarcharger ErrorCode numbers (Victron MPPT error codes), the common ones
static const struct {
  int code;
  const char* text;
} MPPT_ERRORS[] = {
    {1, "Battery temperature too high"},
    {2, "Battery voltage too high"},
    {3, "Remote temp sensor failure"},
    {4, "Remote temp sensor failure"},
    {5, "Remote temp sensor lost"},
    {6, "Remote voltage sense failure"},
    {7, "Remote voltage sense failure"},
    {8, "Remote voltage sense lost"},
    {11, "Battery high ripple voltage"},
    {14, "Battery temperature too low"},
    {17, "Charger temperature too high"},
    {18, "Charger over current"},
    {19, "Charger current reversed"},
    {20, "Bulk time limit exceeded"},
    {21, "Current sensor issue"},
    {26, "Terminals overheated"},
    {28, "Power stage issue"},
    {33, "PV input over voltage"},
    {34, "PV input over current"},
    {67, "BMS connection lost"},
    {114, "CPU temperature too high"},
};

// "HighStarterVoltage" -> "High starter voltage"
static void splitWords(const char* in, char* out, size_t len) {
  size_t n = 0;
  for (size_t i = 0; in[i] && n + 2 < len; i++) {
    char c = in[i];
    if (i > 0 && isupper((unsigned char)c)) {
      out[n++] = ' ';
      c = tolower((unsigned char)c);
    }
    if (c == '/') c = ' ';
    out[n++] = c;
  }
  out[n] = 0;
}

void alarm_text(const VictronData& d, const AlarmEntry& a, char* msg, size_t msgLen, char* dev, size_t devLen) {
  switch (a.device) {
    case AlarmDevice::Quattro: strlcpy(dev, "Quattro", devLen); break;
    case AlarmDevice::Battery: strlcpy(dev, "SmartShunt", devLen); break;
    case AlarmDevice::Mppt: {
      strlcpy(dev, "MPPT", devLen);
      for (const MpptData& m : d.mppt)
        if (m.instance == a.instance && m.name[0]) strlcpy(dev, m.name, devLen);
      break;
    }
  }
  if (a.device == AlarmDevice::Mppt) {
    snprintf(msg, msgLen, "Error %d", a.code);
    for (const auto& e : MPPT_ERRORS)
      if (e.code == a.code) strlcpy(msg, e.text, msgLen);
    return;
  }
  for (const NameText& n : ALARM_NAMES)
    if (!strcmp(n.name, a.name)) {
      strlcpy(msg, n.text, msgLen);
      return;
    }
  splitWords(a.name, msg, msgLen);
}
