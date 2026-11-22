// Network and VRM details entered on the setup page (SPEC.md 6.9), stored in NVS
// namespace "net". None of this is ever in source; logs only show it masked.
#pragma once

#include <Arduino.h>

// The Cerbo announces itself as venus.local on the WiFi it's joined (mDNS)
constexpr const char* DEFAULT_MQTT_HOST = "venus.local";
constexpr uint16_t DEFAULT_MQTT_PORT = 1883;

constexpr size_t DEVICE_NAME_MAX = 20;

struct NetConfig {
  // This display's name, e.g. "Sabrina" or "Dad", so two displays parked near
  // each other can be told apart (setup network, menu, log). Empty = not named.
  String deviceName;
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

// Telling displays apart (SPEC 6.9)
bool device_name_valid(const String& name);    // letters, digits, spaces, dashes; up to DEVICE_NAME_MAX
String device_id_suffix();                     // last 4 hex digits of the board's MAC, e.g. "3F2A"
String setup_ssid(const NetConfig& c);         // "Victron_CYD_Sabrina", or "Victron_CYD_3F2A" unnamed
String device_hostname(const NetConfig& c);    // "cyd-sabrina", or "cyd-3f2a", for mDNS
