// Local mode (SPEC.md 5.1, 5.4): join the Cerbo hotspot, connect to its MQTT
// broker, find the portal ID, subscribe to the 5.5 paths and keep data flowing.
// Runs in its own task on core 0 so blocking connects never freeze the UI.
#pragma once

#include <stddef.h>
#include <stdint.h>

enum class LinkState : uint8_t {
  Off,             // not started (setup mode)
  NoHotspot,       // no hotspot SSID saved
  Scanning,
  HotspotMissing,  // scan didn't find it
  Joining,
  JoinFailed,
  Connecting,
  LoginRejected,   // broker refused the GX username/password
  ConnectFailed,
  FindingPortal,
  NoPortal,        // not discovered and none saved in setup
  Loading,         // subscribed, waiting for full_publish_completed
  Live,
};

struct LinkStatus {
  LinkState state = LinkState::Off;
  int mqttRc = 0;               // PubSubClient state() after a failed connect
  uint32_t lastKeepaliveMs = 0;
  uint32_t messages = 0;
  uint16_t pathsSeen = 0, pathsTotal = 0;
};

void mqtt_local_begin();  // starts the network task
LinkStatus mqtt_local_status();
void mqtt_local_status_text(char* buf, size_t len);
