// Network and VRM details entered on the setup page (SPEC.md 6.9), stored in NVS
// namespace "net". None of this is ever in source; logs only show it masked.
#pragma once

#include <Arduino.h>

constexpr const char* DEFAULT_MQTT_HOST = "172.24.24.1";
constexpr uint16_t DEFAULT_MQTT_PORT = 1883;

struct NetConfig {
  // Local mode: Cerbo hotspot and its MQTT broker
  String hotspotSsid, hotspotPass;
  String mqttHost = DEFAULT_MQTT_HOST;
  uint16_t mqttPort = DEFAULT_MQTT_PORT;
  String gxPass;  // optional, needed by the Cerbo's "Weak" security profile
  // Cloud mode: trailer WiFi and VRM
  String wifiSsid, wifiPass;
  String portalId, vrmUser;
  String vrmToken;  // bare token; "Token " is added when connecting
};

void net_config_load(NetConfig& c);
void net_config_save(const NetConfig& c);
bool net_config_has_network(const NetConfig& c);  // at least one WiFi to join
void net_config_log(const NetConfig& c);           // masked

// "Run setup": set a flag and restart; the next boot opens setup and clears it
void net_config_request_setup();
bool net_config_take_setup_request();

String mask_secret(const String& s);  // "****abcd", or "(empty)"
