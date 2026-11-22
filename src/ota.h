// Firmware updates over WiFi from GitHub releases (SPEC.md 5.7).
// The network task checks about once a day (and when asked from Settings),
// pausing MQTT while it does. An update only installs after a tap on the
// device, only if the release manifest is signed with our key and the file
// matches it, and never to an older version. A new version that doesn't run
// for 60 s is rolled back by the bootloader.
#pragma once

#include <stdint.h>

enum class OtaState : uint8_t {
  Off,            // OTA_REPO not set in include/version.h
  Idle,           // no check yet
  Checking,
  UpToDate,
  Available,      // newer signed version found; waiting for a tap
  CheckFailed,
  Installing,     // downloading and writing; see progress
  InstallFailed,  // still Available to retry
  Restarting,
};

struct OtaStatus {
  OtaState state = OtaState::Idle;
  char latest[16] = "";   // newest version found, e.g. "1.0.1"
  uint8_t progress = 0;   // 0-100 while installing
  char message[48] = "";  // why a check or install failed
  bool rolledBack = false;  // the last update didn't start and was undone
  uint16_t checksDone = 0;   // counts finished checks, so the UI knows when an asked check is done
};

void ota_begin();  // at boot, before anything uses the status
void ota_loop();   // from loop(): keeps a new version once it has run 60 s
OtaStatus ota_status();
const char* ota_version();  // this firmware, e.g. "1.0.0"

// From the UI
void ota_request_check();
void ota_request_install();

// From the network task, with WiFi up
bool ota_work_due();  // a check is due or an install was asked for
void ota_run();       // blocking; MQTT must be disconnected first (frees memory for TLS)
