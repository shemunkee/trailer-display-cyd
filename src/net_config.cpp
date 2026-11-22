#include "net_config.h"

#include <Preferences.h>

static constexpr const char* NVS_NS = "net";

void net_config_load(NetConfig& c) {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  c.deviceName = prefs.getString("name", "");
  c.mqttHost = prefs.getString("mq_host", DEFAULT_MQTT_HOST);
  c.mqttPort = prefs.getUShort("mq_port", DEFAULT_MQTT_PORT);
  c.mqttUser = prefs.getString("mq_user", "");
  c.gxPass = prefs.getString("gx_pass", "");
  c.wifiSsid = prefs.getString("wf_ssid", "");
  c.wifiPass = prefs.getString("wf_pass", "");
  c.portalId = prefs.getString("portal", "");
  c.vrmUser = prefs.getString("vrm_user", "");
  c.vrmToken = prefs.getString("vrm_token", "");
  // Older setups had a separate Cerbo hotspot (hs_ssid, hs_pass) at 172.24.24.1;
  // now there's one WiFi. Use the hotspot only if no WiFi was saved.
  if (c.wifiSsid.isEmpty()) {
    c.wifiSsid = prefs.getString("hs_ssid", "");
    c.wifiPass = prefs.getString("hs_pass", "");
  }
  prefs.end();
  if (c.mqttHost.isEmpty() || c.mqttHost == "172.24.24.1") c.mqttHost = DEFAULT_MQTT_HOST;
  if (c.mqttPort == 0) c.mqttPort = DEFAULT_MQTT_PORT;
}

void net_config_save(const NetConfig& c) {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.remove("hs_ssid");  // from the old two-network setup
  prefs.remove("hs_pass");
  prefs.putString("name", c.deviceName);
  prefs.putString("mq_host", c.mqttHost);
  prefs.putUShort("mq_port", c.mqttPort);
  prefs.putString("mq_user", c.mqttUser);
  prefs.putString("gx_pass", c.gxPass);
  prefs.putString("wf_ssid", c.wifiSsid);
  prefs.putString("wf_pass", c.wifiPass);
  prefs.putString("portal", c.portalId);
  prefs.putString("vrm_user", c.vrmUser);
  prefs.putString("vrm_token", c.vrmToken);
  prefs.end();
}

bool device_name_valid(const String& name) {
  if (name.length() > DEVICE_NAME_MAX) return false;
  for (char ch : name)
    if (!isalnum((unsigned char)ch) && ch != ' ' && ch != '-') return false;
  return true;
}

String device_id_suffix() {
  char buf[8];
  snprintf(buf, sizeof(buf), "%04X", (unsigned)(ESP.getEfuseMac() >> 32) & 0xFFFF);  // from the last two MAC bytes
  return buf;
}

// Spaces become "_" in the network name, so it reads as one word in phone WiFi lists
String setup_ssid(const NetConfig& c) {
  String n = c.deviceName.isEmpty() ? device_id_suffix() : c.deviceName;
  n.replace(' ', '_');
  return "Victron_CYD_" + n;
}

String device_hostname(const NetConfig& c) {
  String n = c.deviceName.isEmpty() ? device_id_suffix() : c.deviceName;
  n.toLowerCase();
  n.replace(' ', '-');
  return "cyd-" + n;
}

bool net_config_has_network(const NetConfig& c) {
  return !c.wifiSsid.isEmpty();
}

String mask_secret(const String& s) {
  if (s.isEmpty()) return "(empty)";
  if (s.length() <= 8) return "****";
  return "****" + s.substring(s.length() - 4);
}

static const char* setOrEmpty(const String& s) { return s.isEmpty() ? "empty" : "set"; }

void net_config_log(const NetConfig& c) {
  Serial.printf("Net: display name %s (setup network %s)\n", c.deviceName.isEmpty() ? "not set" : c.deviceName.c_str(),
                setup_ssid(c).c_str());
  Serial.printf("Net: WiFi %s (pass %s), Cerbo %s:%u (GX user %s, GX pass %s)\n", mask_secret(c.wifiSsid).c_str(),
                setOrEmpty(c.wifiPass), c.mqttHost.c_str(), c.mqttPort, setOrEmpty(c.mqttUser), setOrEmpty(c.gxPass));
  Serial.printf("Net: VRM portal %s, user %s, token %s\n", mask_secret(c.portalId).c_str(),
                mask_secret(c.vrmUser).c_str(),
                c.vrmToken.isEmpty() ? "(empty)" : ("Token " + mask_secret(c.vrmToken)).c_str());
}

void net_config_request_setup() {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  prefs.putBool("setup", true);
  prefs.end();
  Serial.println("Setup requested, restarting");
  delay(100);
  ESP.restart();
}

bool net_config_take_setup_request() {
  Preferences prefs;
  prefs.begin(NVS_NS, false);
  bool requested = prefs.getBool("setup", false);
  if (requested) prefs.remove("setup");
  prefs.end();
  return requested;
}
