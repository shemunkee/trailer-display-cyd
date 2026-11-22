// Data link to the Victron system (SPEC.md 5): one WiFi, shared with the Cerbo.
// Each attempt joins it, tries the Cerbo's broker directly (local), and falls
// back to VRM over the same WiFi (cloud). Runs in its own task on core 0 so
// blocking connects never freeze the UI.
#pragma once

#include <stddef.h>
#include <stdint.h>

enum class LinkMode : uint8_t { None, Local, Cloud };

enum class LinkState : uint8_t {
  Off,             // not started (setup mode)
  NoNetwork,       // nothing saved in setup
  Scanning,
  NetworkMissing,  // saved WiFi not in range
  Joining,
  JoinFailed,
  CerboNotFound,   // local: the Cerbo address didn't resolve (and no cloud to fall back on)
  WrongCerbo,      // local: a Cerbo answered, but its portal ID isn't the one in setup
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
