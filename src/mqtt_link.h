// Data link to the Victron system (SPEC.md 5): local mode through the Cerbo
// hotspot, cloud mode through VRM. Each attempt scans, prefers the hotspot, and
// falls back to the trailer WiFi + VRM. Runs in its own task on core 0 so
// blocking connects never freeze the UI.
#pragma once

#include <stddef.h>
#include <stdint.h>

enum class LinkMode : uint8_t { None, Local, Cloud };

enum class LinkState : uint8_t {
  Off,             // not started (setup mode)
  NoNetwork,       // nothing saved in setup
  Scanning,
  NetworkMissing,  // no saved network in range
  NoCloudConfig,   // trailer WiFi in range, but VRM details missing
  Joining,
  JoinFailed,
  Connecting,
  LoginRejected,   // local: GX login refused; cloud: VRM token rejected
  ConnectFailed,
  FindingPortal,
  NoPortal,        // local: not discovered and none saved in setup
  Loading,         // subscribed, waiting for full_publish_completed
  Live,
};

struct LinkStatus {
  LinkMode mode = LinkMode::None;
  LinkState state = LinkState::Off;
  int mqttRc = 0;  // PubSubClient state() after a failed connect
  uint32_t lastKeepaliveMs = 0;
  uint32_t messages = 0;
  uint16_t pathsSeen = 0, pathsTotal = 0;
};

void mqtt_link_begin();  // starts the network task
LinkStatus mqtt_link_status();
void mqtt_link_status_text(char* buf, size_t len);  // short, without the mode
