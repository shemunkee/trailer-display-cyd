// Latest values from the Victron system (SPEC.md 5.5), filled by the MQTT task
// and read by the screens. A value that hasn't arrived (or came as null, or its
// device went away) is NAN, or -1 for codes, so screens show "--" instead of a
// made-up number. Each group remembers when it last updated, for stale data
// handling (SPEC 5.6, checklist item 11).
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

enum DataGroup : uint8_t { GROUP_BATTERY, GROUP_SOLAR, GROUP_AC, GROUP_QUATTRO, GROUP_TANKS, GROUP_COUNT };

static constexpr int MAX_MPPTS = 4;
static constexpr int MAX_ALARMS = 12;
static constexpr int MAX_TANKS = 4;   // tank services kept; screens show up to 2 LPG tanks (SPEC 6.12)
static constexpr int FLUID_LPG = 8;   // tank FluidType 8 = LPG (Venus dbus wiki)

enum class AlarmDevice : uint8_t { Quattro, Battery, Mppt };

// One active device alarm or warning (SPEC 6.7). Quattro and battery alarms
// are vebus/battery .../Alarms/<name> with 0 none, 1 warning, 2 alarm; the
// Quattro's L1/<name> copy is merged into the same entry. MPPTs report
// solarcharger/<inst>/ErrorCode, a Victron error number; any non-zero code is an alarm.
struct AlarmEntry {
  bool used = false;
  AlarmDevice device = AlarmDevice::Quattro;
  int instance = -1;
  char name[28] = "";        // the part after Alarms/ (L1/ stripped), or "ErrorCode"
  uint8_t variantLevel[2] = {};  // [0] Alarms/<name>, [1] Alarms/L1/<name>
  uint8_t level = 0;         // the higher of the two: 1 warning, 2 alarm
  int code = 0;              // MPPT error code
  uint32_t sinceMs = 0;      // millis() when the display first saw it
  uint32_t serial = 0;       // bigger = newer; changes when it appears or gets worse
};

struct MpptData {
  int instance = -1;  // -1 = slot unused
  char name[24] = "";  // CustomName; matched by name, never by instance (SPEC 4)
  float power = NAN, pvVolts = NAN, yieldTodayKwh = NAN;
  int state = -1, errorCode = -1;
};

// One tank service (SPEC 5.5): tank/<inst>/Level, Remaining, Capacity, CustomName,
// FluidType, Status. Mopeka Pro LPG sensors reach the Cerbo over Bluetooth.
struct TankData {
  int instance = -1;    // -1 = slot unused
  char name[32] = "";   // CustomName, e.g. "Driver Side Tank"
  float level = NAN;    // percent, 0-100
  float remainingM3 = NAN, capacityM3 = NAN;
  int fluidType = -1;   // FLUID_LPG = 8
  int status = -1;      // 0 OK, 1 Disconnected, 2 Short circuited, 3 Unknown, 4 Configuration error
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
  // Tanks
  TankData tanks[MAX_TANKS];

  uint32_t updatedMs[GROUP_COUNT] = {};  // millis() of the last value per group, 0 = never
  AlarmEntry alarms[MAX_ALARMS];
  uint32_t alarmSerial = 0;  // last serial handed out: grows on every new or worse alarm

  uint32_t lastMessageMs = 0;            // millis() of the last message of any kind, 0 = never (SPEC 5.6)
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

// Up to `max` LPG tanks, sorted by CustomName (Driver before Passenger). Returns how
// many were put in `out`; `total` (optional) gets how many LPG tanks there are in all.
int lpg_tanks(const VictronData& d, const TankData** out, int max, int* total = nullptr);
const char* tank_status_name(int status);  // short, for the card: OK, No signal, Shorted, Unknown, Config error
const char* tank_status_full(int status);  // the Venus name, for the log: Disconnected, Short circuited, ...
float tank_pounds(const TankData& t);      // propane left in lb from Remaining (m3), NAN if unknown
bool tank_reading_ok(const TankData& t);   // Status OK and Level present

// Readable text for an alarm: message ("Overload") and device ("Quattro", or the MPPT's name)
void alarm_text(const VictronData& d, const AlarmEntry& a, char* msg, size_t msgLen, char* dev, size_t devLen);
