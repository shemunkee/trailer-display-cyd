// Network and VRM details entered on the setup page (SPEC.md 6.9), stored in NVS
// namespace "net". None of this is ever in source; logs only show it masked.
#pragma once

#include <Arduino.h>

// The Cerbo announces itself as venus.local on the WiFi it's joined (mDNS)
constexpr const char* DEFAULT_MQTT_HOST = "venus.local";
constexpr uint16_t DEFAULT_MQTT_PORT = 1883;

struct NetConfig {
  // The trailer WiFi; the Cerbo and the display both join it (SPEC 5)
  String wifiSsid, wifiPass;
  // Local mode: the Cerbo's MQTT broker on that WiFi
  String mqttHost = DEFAULT_MQTT_HOST;
  uint16_t mqttPort = DEFAULT_MQTT_PORT;
  String mqttUser;  // optional; sent with gxPass for the Cerbo's "Weak" security profile
  String gxPass;
  // Cloud mode: VRM over the same WiFi
  String portalId, vrmUser;
  String vrmToken;  // bare token; "Token " is added when connecting
};

void net_config_load(NetConfig& c);
void net_config_save(const NetConfig& c);
bool net_config_has_network(const NetConfig& c);  // a WiFi to join
void net_config_log(const NetConfig& c);           // masked

// "Run setup": set a flag and restart; the next boot opens setup and clears it
void net_config_request_setup();
bool net_config_take_setup_request();

String mask_secret(const String& s);  // "****abcd", or "(empty)"
