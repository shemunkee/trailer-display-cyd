// Latest values from the Victron system (SPEC.md 5.5), filled by the MQTT task
// and read by the screens. A value that hasn't arrived (or came as null, or its
// device went away) is NAN, or -1 for codes, so screens show "--" instead of a
// made-up number. Each group remembers when it last updated, for stale data
// handling (SPEC 5.6, checklist item 11).
#pragma once

#include <math.h>
#include <stdint.h>

enum DataGroup : uint8_t { GROUP_BATTERY, GROUP_SOLAR, GROUP_AC, GROUP_QUATTRO, GROUP_COUNT };

static constexpr int MAX_MPPTS = 4;

struct MpptData {
  int instance = -1;  // -1 = slot unused
  char name[24] = "";  // CustomName; matched by name, never by instance (SPEC 4)
  float power = NAN, pvVolts = NAN, yieldTodayKwh = NAN;
  int state = -1, errorCode = -1;
};

struct VictronData {
  // Battery
  float soc = NAN, battVolts = NAN, battAmps = NAN, battWatts = NAN;
  float timeToGoSec = NAN;  // null while charging
  float consumedAh = NAN;
  // Solar
  float pvWatts = NAN;
  MpptData mppt[MAX_MPPTS];
  // AC
  int acSource = -1;  // system/0/Ac/ActiveIn/Source: 1 Grid, 2 Generator, 3 Shore, 240 none
  float gridWatts = NAN, gensetWatts = NAN, loadWatts = NAN;
  float acInVolts = NAN;
  float acOutVolts = NAN, acOutAmps = NAN, acOutHz = NAN, inputLimitAmps = NAN;
  // Quattro
  int vebusState = -1;

  uint32_t updatedMs[GROUP_COUNT] = {};  // millis() of the last value per group, 0 = never
};

// Net task: path is relative to N/<portalId>/; payload is {"value": ...} or empty
void victron_data_on_message(const char* path, const uint8_t* payload, unsigned len);

// UI: a consistent copy of everything
void victron_data_get(VictronData& out);

// Helpers for the screens
const char* vebus_state_name(int state);  // Off, Inverting, Bulk, ...
bool vebus_state_charging(int state);     // Bulk, Absorption, Float, Storage, Equalize
const char* mppt_state_name(int state);   // Off, Bulk, Absorption, Float, ...

// The MPPT whose CustomName contains `name` (case-insensitive), or nullptr
const MpptData* mppt_by_name(const VictronData& d, const char* name);
bool mppt_name_matches(const MpptData* m, const char* name);
