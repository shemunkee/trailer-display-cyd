#include "mqtt_local.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include "net_config.h"

static constexpr uint32_t JOIN_TIMEOUT_MS = 15000;    // SPEC 5.3
static constexpr uint32_t DISCOVERY_MS = 10000;
static constexpr uint32_t KEEPALIVE_MS = 30000;       // broker timeout is 60 s
static constexpr uint32_t RETRY_MIN_MS = 5000;
static constexpr uint32_t RETRY_MAX_MS = 60000;
static constexpr uint32_t UNSEEN_REPORT_MS = 60000;

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
static WiFiClient net;
static PubSubClient mqtt(net);
static SemaphoreHandle_t lock;
static LinkStatus status;
static bool seen[PATH_COUNT];

static String portalId;     // the one in use this session
static String discoveredId;
static uint32_t lastKeepalive = 0, lastUnseenReport = 0;

// ---------- status, shared with the UI task ----------

static void setState(LinkState s, int rc = 0) {
  xSemaphoreTake(lock, portMAX_DELAY);
  status.state = s;
  status.mqttRc = rc;
  xSemaphoreGive(lock);
}

LinkStatus mqtt_local_status() {
  LinkStatus s;
  if (!lock) return s;
  xSemaphoreTake(lock, portMAX_DELAY);
  s = status;
  xSemaphoreGive(lock);
  return s;
}

void mqtt_local_status_text(char* buf, size_t len) {
  LinkStatus s = mqtt_local_status();
  switch (s.state) {
    case LinkState::Off: snprintf(buf, len, "Local: off"); break;
    case LinkState::NoHotspot: snprintf(buf, len, "Local: no hotspot saved in setup"); break;
    case LinkState::Scanning: snprintf(buf, len, "Local: looking for hotspot"); break;
    case LinkState::HotspotMissing: snprintf(buf, len, "Local: hotspot not in range"); break;
    case LinkState::Joining: snprintf(buf, len, "Local: joining hotspot"); break;
    case LinkState::JoinFailed: snprintf(buf, len, "Local: couldn't join hotspot, check password"); break;
    case LinkState::Connecting: snprintf(buf, len, "Local: connecting to broker"); break;
    case LinkState::LoginRejected: snprintf(buf, len, "Local: broker rejected GX login"); break;
    case LinkState::ConnectFailed: snprintf(buf, len, "Local: broker not reachable (%d)", s.mqttRc); break;
    case LinkState::FindingPortal: snprintf(buf, len, "Local: finding portal ID"); break;
    case LinkState::NoPortal: snprintf(buf, len, "Local: portal ID not found, add it in setup"); break;
    case LinkState::Loading:
      snprintf(buf, len, "Local: loading, %u/%u paths", s.pathsSeen, s.pathsTotal);
      break;
    case LinkState::Live:
      snprintf(buf, len, "Local: live, %u/%u paths, keepalive %lus ago", s.pathsSeen, s.pathsTotal,
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

  // Discovery: N/<portalId>/system/0/Serial
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

static bool joinHotspot() {
  setState(LinkState::Scanning);
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  bool found = false;
  for (int i = 0; i < n && !found; i++) found = WiFi.SSID(i) == cfg.hotspotSsid;
  WiFi.scanDelete();
  if (!found) {
    Serial.println("Local: hotspot not found in scan");
    setState(LinkState::HotspotMissing);
    return false;
  }

  setState(LinkState::Joining);
  WiFi.begin(cfg.hotspotSsid.c_str(), cfg.hotspotPass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < JOIN_TIMEOUT_MS) vTaskDelay(pdMS_TO_TICKS(100));
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Local: couldn't join hotspot");
    WiFi.disconnect(true);
    setState(LinkState::JoinFailed);
    return false;
  }
  Serial.printf("Local: joined hotspot, IP %s\n", WiFi.localIP().toString().c_str());
  return true;
}

static bool connectBroker() {
  setState(LinkState::Connecting);
  mqtt.setServer(cfg.mqttHost.c_str(), cfg.mqttPort);
  String id = "cyd-victron-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok;
  if (cfg.gxPass.isEmpty()) ok = mqtt.connect(id.c_str());
  else ok = mqtt.connect(id.c_str(), cfg.mqttUser.c_str(), cfg.gxPass.c_str());
  if (ok) {
    Serial.printf("Local: broker %s:%u connected\n", cfg.mqttHost.c_str(), cfg.mqttPort);
    return true;
  }
  int rc = mqtt.state();
  Serial.printf("Local: broker connect failed, state %d\n", rc);
  // 4 = bad username/password, 5 = not authorized
  setState(rc == 4 || rc == 5 ? LinkState::LoginRejected : LinkState::ConnectFailed, rc);
  return false;
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
  Serial.printf("Local: subscribed to %d paths, keepalive sent\n", PATH_COUNT);
}

// ---------- task ----------

static void netTask(void*) {
  uint32_t retry = RETRY_MIN_MS;
  for (;;) {
    bool ok = (WiFi.status() == WL_CONNECTED || joinHotspot()) && connectBroker() && findPortal();
    if (!ok) {
      mqtt.disconnect();
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
    Serial.println("Local: connection lost");
    mqtt.disconnect();
  }
}

void mqtt_local_begin() {
  net_config_load(cfg);
  lock = xSemaphoreCreateMutex();
  if (cfg.hotspotSsid.isEmpty()) {
    status.state = LinkState::NoHotspot;
    Serial.println("Local: no hotspot saved, not starting");
    return;
  }
  mqtt.setBufferSize(1024);
  mqtt.setSocketTimeout(5);
  mqtt.setCallback(onMessage);
  xTaskCreatePinnedToCore(netTask, "net", 8192, nullptr, 1, nullptr, 0);
}
