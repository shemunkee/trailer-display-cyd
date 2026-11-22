#include "mqtt_link.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "net_config.h"
#include "venus_ca.h"

static constexpr uint32_t JOIN_TIMEOUT_MS = 15000;    // SPEC 5.3
static constexpr uint32_t DISCOVERY_MS = 10000;
static constexpr uint32_t KEEPALIVE_MS = 30000;       // broker timeout is 60 s
static constexpr uint32_t RETRY_MIN_MS = 5000;
static constexpr uint32_t RETRY_MAX_MS = 60000;
static constexpr uint32_t UNSEEN_REPORT_MS = 60000;
static constexpr uint16_t CLOUD_PORT = 8883;
static constexpr unsigned long TLS_HANDSHAKE_S = 15;

// SPEC 5.5 paths, relative to N/<portalId>/. Unverified until checklist item 5.
static const char* const PATHS[] = {
    "system/0/Dc/Battery/Soc",
    "system/0/Dc/Battery/Voltage",
    "system/0/Dc/Battery/Current",
    "system/0/Dc/Battery/Power",
    "system/0/Dc/Battery/TimeToGo",
    "battery/+/ConsumedAmphours",
    "system/0/Dc/Pv/Power",
    "solarcharger/+/CustomName",
    "solarcharger/+/Yield/Power",
    "solarcharger/+/Pv/V",
    "solarcharger/+/State",
    "solarcharger/+/History/Daily/0/Yield",
    "system/0/Ac/ActiveIn/Source",
    "system/0/Ac/Grid/L1/Power",
    "system/0/Ac/Genset/L1/Power",
    "system/0/Ac/Consumption/L1/Power",
    "vebus/+/Ac/Out/L1/V",
    "vebus/+/Ac/Out/L1/I",
    "vebus/+/Ac/Out/L1/F",
    "vebus/+/Ac/ActiveIn/CurrentLimit",
    "vebus/+/State",
    "vebus/+/Alarms/#",
    "battery/+/Alarms/#",
    "solarcharger/+/ErrorCode",
};
static constexpr int PATH_COUNT = sizeof(PATHS) / sizeof(PATHS[0]);

static NetConfig cfg;
static WiFiClient plainNet;
static WiFiClientSecure tlsNet;
static PubSubClient mqtt;
static SemaphoreHandle_t lock;
static LinkStatus status;
static bool seen[PATH_COUNT];

static LinkMode mode = LinkMode::None;
static String portalId;     // the one in use this session
static String discoveredId;
static String cloudHost;
static uint32_t lastKeepalive = 0, lastUnseenReport = 0;

// ---------- status, shared with the UI task ----------

static void setState(LinkState s, int rc = 0) {
  xSemaphoreTake(lock, portMAX_DELAY);
  status.mode = mode;
  status.state = s;
  status.mqttRc = rc;
  xSemaphoreGive(lock);
}

LinkStatus mqtt_link_status() {
  LinkStatus s;
  if (!lock) return s;
  xSemaphoreTake(lock, portMAX_DELAY);
  s = status;
  xSemaphoreGive(lock);
  return s;
}

void mqtt_link_status_text(char* buf, size_t len) {
  LinkStatus s = mqtt_link_status();
  const char* m = s.mode == LinkMode::Cloud ? "Cloud" : "Local";
  bool cloud = s.mode == LinkMode::Cloud;
  switch (s.state) {
    case LinkState::Off: snprintf(buf, len, "Data: off"); break;
    case LinkState::NoNetwork: snprintf(buf, len, "No network saved in setup"); break;
    case LinkState::Scanning: snprintf(buf, len, "Looking for networks"); break;
    case LinkState::NetworkMissing: snprintf(buf, len, "No saved network in range"); break;
    case LinkState::NoCloudConfig: snprintf(buf, len, "Cloud: VRM details missing in setup"); break;
    case LinkState::Joining: snprintf(buf, len, "%s: joining %s", m, cloud ? "trailer WiFi" : "hotspot"); break;
    case LinkState::JoinFailed: snprintf(buf, len, "%s: couldn't join WiFi, check password", m); break;
    case LinkState::Connecting: snprintf(buf, len, "%s: connecting to broker", m); break;
    case LinkState::LoginRejected:
      snprintf(buf, len, cloud ? "Cloud: VRM token rejected" : "Local: broker rejected GX login");
      break;
    case LinkState::ConnectFailed: snprintf(buf, len, "%s: broker not reachable (%d)", m, s.mqttRc); break;
    case LinkState::FindingPortal: snprintf(buf, len, "%s: finding portal ID", m); break;
    case LinkState::NoPortal: snprintf(buf, len, "%s: portal ID not found, add it in setup", m); break;
    case LinkState::Loading:
      snprintf(buf, len, "%s: loading, %u/%u paths", m, s.pathsSeen, s.pathsTotal);
      break;
    case LinkState::Live:
      snprintf(buf, len, "%s: live, %u/%u paths, keepalive %lus ago", m, s.pathsSeen, s.pathsTotal,
               (unsigned long)((millis() - s.lastKeepaliveMs) / 1000));
      break;
  }
}

// ---------- helpers ----------

// MQTT filter match: '+' is one level, '#' is the rest
static bool matches(const char* filter, const char* topic) {
  while (*filter) {
    if (*filter == '#') return true;
    if (*filter == '+') {
      while (*topic && *topic != '/') topic++;
      filter++;
    } else {
      if (*filter != *topic) return false;
      filter++;
      topic++;
    }
  }
  return *topic == 0;
}

// Topics in logs show <id> instead of the portal ID (SPEC 7)
static String masked(const String& topic) {
  String t = topic;
  if (!portalId.isEmpty()) t.replace(portalId, "<id>");
  if (!discoveredId.isEmpty()) t.replace(discoveredId, "<id>");
  return t;
}

// VRM picks the broker from the portal ID: sum of its lowercase characters mod 128 (SPEC 5.2)
static String brokerFor(String id) {
  id.toLowerCase();
  uint32_t sum = 0;
  for (unsigned int i = 0; i < id.length(); i++) sum += (uint8_t)id[i];
  return "mqtt" + String(sum % 128) + ".victronenergy.com";
}

static bool cloudConfigured() {
  return !cfg.wifiSsid.isEmpty() && !cfg.portalId.isEmpty() && !cfg.vrmUser.isEmpty() && !cfg.vrmToken.isEmpty();
}

// Read only (SPEC 5.4, 7): the only publish path, and it refuses anything but R/
static bool publishRead(const String& topic, const char* payload) {
  if (!topic.startsWith("R/")) {
    Serial.printf("MQTT: blocked publish to %s (read only)\n", masked(topic).c_str());
    return false;
  }
  return mqtt.publish(topic.c_str(), payload);
}

// First keepalive is empty so the Cerbo sends everything once; later ones skip that
static void sendKeepalive(bool full) {
  publishRead("R/" + portalId + "/keepalive", full ? "" : "{\"keepalive-options\":[\"suppress-republish\"]}");
  lastKeepalive = millis();
  xSemaphoreTake(lock, portMAX_DELAY);
  status.lastKeepaliveMs = lastKeepalive;
  xSemaphoreGive(lock);
}

// After a full publish, list what's missing (or say all arrived); later, only
// repeat the list while something is still missing
static void reportUnseen(bool afterFullPublish) {
  int missing = 0;
  for (int i = 0; i < PATH_COUNT; i++)
    if (!seen[i]) {
      if (!missing++) Serial.println("MQTT: paths with no data yet:");
      Serial.printf("  %s\n", PATHS[i]);
    }
  if (!missing && afterFullPublish) Serial.println("MQTT: all paths have data");
  lastUnseenReport = millis();
}

// ---------- incoming ----------

static void onMessage(char* topicRaw, byte* payload, unsigned int len) {
  String topic(topicRaw);
  xSemaphoreTake(lock, portMAX_DELAY);
  status.messages++;
  xSemaphoreGive(lock);

  // Discovery (local only): N/<portalId>/system/0/Serial
  if (portalId.isEmpty()) {
    if (topic.startsWith("N/") && topic.endsWith("/system/0/Serial")) {
      discoveredId = topic.substring(2, topic.indexOf('/', 2));
    }
    return;
  }

  String base = "N/" + portalId + "/";
  if (!topic.startsWith(base)) return;
  String path = topic.substring(base.length());

  if (path == "full_publish_completed") {
    setState(LinkState::Live);
    Serial.println("MQTT: full publish completed");
    reportUnseen(true);
    return;
  }

  for (int i = 0; i < PATH_COUNT; i++) {
    if (seen[i] || !matches(PATHS[i], path.c_str())) continue;
    seen[i] = true;
    xSemaphoreTake(lock, portMAX_DELAY);
    status.pathsSeen++;
    xSemaphoreGive(lock);
    // Values aren't secret; payload is {"value": ...}, or empty if the device went away
    String value = len ? String((const char*)payload, min(len, 64u)) : String("(empty: device gone)");
    Serial.printf("MQTT: seen %s = %s\n", path.c_str(), value.c_str());
  }
}

// ---------- connection steps ----------

struct ScanResult {
  bool hotspot, trailer;
};

static ScanResult scan() {
  mode = LinkMode::None;
  setState(LinkState::Scanning);
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  ScanResult r = {false, false};
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (!cfg.hotspotSsid.isEmpty() && ssid == cfg.hotspotSsid) r.hotspot = true;
    if (!cfg.wifiSsid.isEmpty() && ssid == cfg.wifiSsid) r.trailer = true;
  }
  WiFi.scanDelete();
  Serial.printf("Scan: hotspot %s, trailer WiFi %s\n", r.hotspot ? "in range" : "not found",
                r.trailer ? "in range" : "not found");
  return r;
}

static bool join(const String& ssid, const String& pass) {
  setState(LinkState::Joining);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < JOIN_TIMEOUT_MS) vTaskDelay(pdMS_TO_TICKS(100));
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("%s: couldn't join WiFi\n", mode == LinkMode::Cloud ? "Cloud" : "Local");
    WiFi.disconnect(true);
    setState(LinkState::JoinFailed);
    return false;
  }
  Serial.printf("%s: joined WiFi, IP %s\n", mode == LinkMode::Cloud ? "Cloud" : "Local",
                WiFi.localIP().toString().c_str());
  return true;
}

static String clientId() { return "cyd-victron-" + String((uint32_t)ESP.getEfuseMac(), HEX); }

static void connectFailed(const char* label, bool tls) {
  int rc = mqtt.state();
  Serial.printf("%s: broker connect failed, state %d\n", label, rc);
  if (tls) {
    char err[100];
    if (tlsNet.lastError(err, sizeof(err))) Serial.printf("%s: TLS error: %s\n", label, err);
  }
  // 4 = bad username/password, 5 = not authorized
  setState(rc == 4 || rc == 5 ? LinkState::LoginRejected : LinkState::ConnectFailed, rc);
}

static bool connectLocal() {
  setState(LinkState::Connecting);
  mqtt.setClient(plainNet);
  mqtt.setServer(cfg.mqttHost.c_str(), cfg.mqttPort);
  bool ok = cfg.gxPass.isEmpty() ? mqtt.connect(clientId().c_str())
                                 : mqtt.connect(clientId().c_str(), cfg.mqttUser.c_str(), cfg.gxPass.c_str());
  if (!ok) {
    connectFailed("Local", false);
    return false;
  }
  Serial.printf("Local: broker %s:%u connected\n", cfg.mqttHost.c_str(), cfg.mqttPort);
  return true;
}

// TLS verified against the Victron CA, always (SPEC 5.2, 7)
static bool connectCloud() {
  setState(LinkState::Connecting);
  cloudHost = brokerFor(cfg.portalId);
  mqtt.setClient(tlsNet);
  mqtt.setServer(cloudHost.c_str(), CLOUD_PORT);
  String password = "Token " + cfg.vrmToken;
  Serial.printf("Cloud: connecting to %s:%u as %s, Token %s\n", cloudHost.c_str(), CLOUD_PORT,
                mask_secret(cfg.vrmUser).c_str(), mask_secret(cfg.vrmToken).c_str());
  if (!mqtt.connect(clientId().c_str(), cfg.vrmUser.c_str(), password.c_str())) {
    connectFailed("Cloud", true);
    return false;
  }
  Serial.println("Cloud: broker connected, certificate verified");
  return true;
}

// Prefer the ID the Cerbo announces; fall back to the one saved in setup
static bool findPortal() {
  setState(LinkState::FindingPortal);
  discoveredId = "";
  portalId = "";
  mqtt.subscribe("N/+/system/0/Serial");
  uint32_t start = millis();
  while (discoveredId.isEmpty() && millis() - start < DISCOVERY_MS && mqtt.connected()) {
    mqtt.loop();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  mqtt.unsubscribe("N/+/system/0/Serial");

  if (!discoveredId.isEmpty()) {
    portalId = discoveredId;
    Serial.printf("Local: discovered portal ID %s\n", mask_secret(portalId).c_str());
    if (!cfg.portalId.isEmpty() && cfg.portalId != portalId)
      Serial.printf("Local: warning, setup has a different portal ID (%s); using the discovered one\n",
                    mask_secret(cfg.portalId).c_str());
  } else if (!cfg.portalId.isEmpty()) {
    portalId = cfg.portalId;
    Serial.printf("Local: no portal ID announced, using the one from setup (%s)\n", mask_secret(portalId).c_str());
  } else {
    Serial.println("Local: portal ID not announced and none saved in setup");
    setState(LinkState::NoPortal);
    return false;
  }
  return true;
}

// One attempt: hotspot first (SPEC 5.3 steps 1-2), else trailer WiFi + VRM.
// Rescanning while on cloud to switch back is checklist item 7.
static bool connectOnce() {
  ScanResult r = scan();
  if (r.hotspot) {
    mode = LinkMode::Local;
    if (join(cfg.hotspotSsid, cfg.hotspotPass) && connectLocal() && findPortal()) return true;
    mqtt.disconnect();
    WiFi.disconnect(true);
  }
  if (r.trailer) {
    mode = LinkMode::Cloud;
    if (!cloudConfigured()) {
      Serial.println("Cloud: trailer WiFi in range but VRM portal ID, email or token missing");
      setState(LinkState::NoCloudConfig);
      return false;
    }
    if (join(cfg.wifiSsid, cfg.wifiPass) && connectCloud()) {
      portalId = cfg.portalId;
      return true;
    }
    mqtt.disconnect();
    WiFi.disconnect(true);
    return false;
  }
  if (!r.hotspot) setState(LinkState::NetworkMissing);
  return false;
}

static void subscribeAll() {
  memset(seen, 0, sizeof(seen));
  xSemaphoreTake(lock, portMAX_DELAY);
  status.pathsSeen = 0;
  status.pathsTotal = PATH_COUNT;
  xSemaphoreGive(lock);
  String base = "N/" + portalId + "/";
  for (int i = 0; i < PATH_COUNT; i++) mqtt.subscribe((base + PATHS[i]).c_str());
  mqtt.subscribe((base + "full_publish_completed").c_str());
  setState(LinkState::Loading);
  sendKeepalive(true);
  Serial.printf("%s: subscribed to %d paths, keepalive sent\n", mode == LinkMode::Cloud ? "Cloud" : "Local",
                PATH_COUNT);
}

// ---------- task ----------

static void netTask(void*) {
  uint32_t retry = RETRY_MIN_MS;
  for (;;) {
    if (!connectOnce()) {
      vTaskDelay(pdMS_TO_TICKS(retry));
      retry = min(retry * 2, RETRY_MAX_MS);
      continue;
    }
    retry = RETRY_MIN_MS;
    subscribeAll();

    while (mqtt.connected() && WiFi.status() == WL_CONNECTED) {
      mqtt.loop();
      if (millis() - lastKeepalive > KEEPALIVE_MS) sendKeepalive(false);
      if (millis() - lastUnseenReport > UNSEEN_REPORT_MS) reportUnseen(false);
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    Serial.printf("%s: connection lost\n", mode == LinkMode::Cloud ? "Cloud" : "Local");
    mqtt.disconnect();
    WiFi.disconnect(true);
  }
}

void mqtt_link_begin() {
  net_config_load(cfg);
  lock = xSemaphoreCreateMutex();
  if (!net_config_has_network(cfg)) {
    status.state = LinkState::NoNetwork;
    Serial.println("Link: no network saved, not starting");
    return;
  }
  tlsNet.setCACert(VENUS_CA);
  tlsNet.setHandshakeTimeout(TLS_HANDSHAKE_S);
  mqtt.setBufferSize(1024);
  mqtt.setSocketTimeout(5);
  mqtt.setCallback(onMessage);
  // TLS needs more stack than plain MQTT
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);
}
