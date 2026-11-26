#include "mqtt_link.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "net_config.h"
#include "ota.h"
#include "venus_ca.h"
#include "victron_data.h"

static constexpr uint32_t JOIN_TIMEOUT_MS = 15000;    // SPEC 5.3
static constexpr uint32_t DISCOVERY_MS = 10000;
static constexpr uint32_t MDNS_TIMEOUT_MS = 2000;   // venus.local lookup
static constexpr int32_t PROBE_TIMEOUT_MS = 3000;   // is the Cerbo broker answering?
static constexpr uint32_t KEEPALIVE_MS = 30000;       // broker timeout is 60 s
static constexpr uint32_t RETRY_MIN_MS = 5000;
static constexpr uint32_t RETRY_MAX_MS = 60000;
static constexpr uint32_t UNSEEN_REPORT_MS = 60000;
static constexpr uint32_t RESCAN_MS = 120000;           // SPEC 5.3 step 3
static constexpr uint32_t RESCAN_MAX_MS = 30 * 60000;   // cap after repeated local failures
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
    "vebus/+/Ac/ActiveIn/L1/V",
    "vebus/+/Ac/Out/L1/V",
    "vebus/+/Ac/Out/L1/I",
    "vebus/+/Ac/Out/L1/F",
    "vebus/+/Ac/ActiveIn/CurrentLimit",
    "vebus/+/State",
    "vebus/+/Alarms/#",
    "battery/+/Alarms/#",
    "solarcharger/+/ErrorCode",
    // Tanks (SPEC 5.5): Mopeka LPG sensors; instance not known ahead, one-level wildcard only
    "tank/+/Level",
    "tank/+/Remaining",
    "tank/+/Capacity",
    "tank/+/CustomName",
    "tank/+/FluidType",
    "tank/+/Status",
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
static uint32_t lastRescan = 0;
static uint8_t localFailStreak = 0;  // Cerbo answered the probe but local didn't connect
static bool mdnsStarted = false;
static IPAddress cerboIp;

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

// Short status for the Settings Data source row; the mode (Local/Cloud) is shown separately
void mqtt_link_status_text(char* buf, size_t len) {
  LinkStatus s = mqtt_link_status();
  bool cloud = s.mode == LinkMode::Cloud;
  switch (s.state) {
    case LinkState::Off: snprintf(buf, len, "Off"); break;
    case LinkState::NoNetwork: snprintf(buf, len, "No WiFi saved in setup"); break;
    case LinkState::Scanning: snprintf(buf, len, "Looking for WiFi"); break;
    case LinkState::NetworkMissing: snprintf(buf, len, "Saved WiFi not in range"); break;
    case LinkState::Joining: snprintf(buf, len, "Joining WiFi"); break;
    case LinkState::JoinFailed: snprintf(buf, len, "Couldn't join WiFi"); break;
    case LinkState::CerboNotFound: snprintf(buf, len, "Cerbo not found on WiFi"); break;
    case LinkState::WrongCerbo: snprintf(buf, len, "Different Cerbo found (portal ID)"); break;
    case LinkState::Connecting: snprintf(buf, len, "Connecting"); break;
    case LinkState::LoginRejected: snprintf(buf, len, cloud ? "VRM token rejected" : "GX login rejected"); break;
    case LinkState::ConnectFailed:
      snprintf(buf, len, "%s not reachable (%d)", cloud ? "VRM broker" : "Cerbo broker", s.mqttRc);
      break;
    case LinkState::FindingPortal: snprintf(buf, len, "Finding portal ID"); break;
    case LinkState::NoPortal: snprintf(buf, len, "Portal ID missing in setup"); break;
    case LinkState::Loading: snprintf(buf, len, "Loading, %u/%u paths", s.pathsSeen, s.pathsTotal); break;
    case LinkState::Live: snprintf(buf, len, "Live, %u/%u paths", s.pathsSeen, s.pathsTotal); break;
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

  victron_data_on_message(path.c_str(), payload, len);

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

// Join the saved WiFi, unless still joined from the last session (a switch
// from cloud to local keeps the WiFi up)
static bool ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  mode = LinkMode::None;
  setState(LinkState::Scanning);
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  bool found = false;
  for (int i = 0; i < n && !found; i++) found = WiFi.SSID(i) == cfg.wifiSsid;
  WiFi.scanDelete();
  if (!found) {
    Serial.println("WiFi: saved network not in range");
    setState(LinkState::NetworkMissing);
    return false;
  }
  setState(LinkState::Joining);
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < JOIN_TIMEOUT_MS) vTaskDelay(pdMS_TO_TICKS(100));
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi: couldn't join");
    WiFi.disconnect(true);
    setState(LinkState::JoinFailed);
    return false;
  }
  Serial.printf("WiFi: joined, IP %s\n", WiFi.localIP().toString().c_str());
  // Needed to look up venus.local; our own name tells displays apart on the network
  if (!mdnsStarted) mdnsStarted = MDNS.begin(device_hostname(cfg).c_str());
  return true;
}

// The Cerbo's address: an IP as typed, a .local name through mDNS, or plain DNS
static bool resolveCerbo(IPAddress& ip) {
  if (ip.fromString(cfg.mqttHost)) return true;
  String host = cfg.mqttHost;
  if (host.endsWith(".local")) {
    if (!mdnsStarted) return false;
    ip = MDNS.queryHost(host.substring(0, host.length() - 6), MDNS_TIMEOUT_MS);
    return ip != IPAddress((uint32_t)0);
  }
  return WiFi.hostByName(host.c_str(), ip) == 1;
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
  mode = LinkMode::Local;
  setState(LinkState::Connecting);
  IPAddress ip;
  if (!resolveCerbo(ip)) {
    Serial.printf("Local: Cerbo %s not found on this WiFi\n", cfg.mqttHost.c_str());
    setState(LinkState::CerboNotFound);
    return false;
  }
  cerboIp = ip;
  mqtt.setClient(plainNet);
  mqtt.setServer(cerboIp, cfg.mqttPort);
  bool ok = cfg.gxPass.isEmpty() ? mqtt.connect(clientId().c_str())
                                 : mqtt.connect(clientId().c_str(), cfg.mqttUser.c_str(), cfg.gxPass.c_str());
  if (!ok) {
    connectFailed("Local", false);
    return false;
  }
  Serial.printf("Local: broker %s (%s):%u connected\n", cfg.mqttHost.c_str(), cerboIp.toString().c_str(),
                cfg.mqttPort);
  return true;
}

// TLS verified against the Victron CA, always (SPEC 5.2, 7)
static bool connectCloud() {
  mode = LinkMode::Cloud;
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
    Serial.printf("Local: discovered portal ID %s\n", mask_secret(discoveredId).c_str());
    // Another trailer's Cerbo on the same WiFi (venus.local can find either):
    // refuse it rather than show someone else's system (SPEC 5.4)
    if (!cfg.portalId.isEmpty() && !cfg.portalId.equalsIgnoreCase(discoveredId)) {
      Serial.printf("Local: that's not our Cerbo (setup has %s); not using it. If two Cerbos share this WiFi, "
                    "put ours by IP address in setup instead of %s\n",
                    mask_secret(cfg.portalId).c_str(), cfg.mqttHost.c_str());
      setState(LinkState::WrongCerbo);
      return false;
    }
    portalId = discoveredId;
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

// One attempt (SPEC 5.3): join the WiFi, try the Cerbo directly, else VRM over the same WiFi
static bool connectOnce() {
  if (!ensureWifi()) return false;
  if (connectLocal() && findPortal()) {
    localFailStreak = 0;
    return true;
  }
  mqtt.disconnect();
  if (!cloudConfigured()) {
    Serial.println("Cloud: not set up (VRM portal ID, email or token missing), no fallback");
    return false;  // keeps the local failure as the status
  }
  if (connectCloud()) {
    portalId = cfg.portalId;
    lastRescan = millis();
    return true;
  }
  mqtt.disconnect();
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
  // For bring-up: how close the net task came to its stack limit (TLS is the hungry part)
  Serial.printf("Net: task stack %u bytes spare\n", (unsigned)uxTaskGetStackHighWaterMark(nullptr));
}

// While on cloud (SPEC 5.3 step 3): every 2 min, check whether the Cerbo's
// broker answers on the WiFi, without dropping the cloud connection. If it
// answers but local then fails, the wait doubles (up to 30 min), so a Cerbo
// that accepts connections but not our login doesn't keep knocking cloud offline.
static bool shouldSwitchToLocal() {
  if (mode != LinkMode::Cloud) return false;
  uint32_t wait = RESCAN_MS << min<uint8_t>(localFailStreak, 4);
  if (wait > RESCAN_MAX_MS) wait = RESCAN_MAX_MS;
  if (millis() - lastRescan < wait) return false;
  lastRescan = millis();
  IPAddress ip;
  if (!resolveCerbo(ip)) return false;
  WiFiClient probe;
  bool open = probe.connect(ip, cfg.mqttPort, PROBE_TIMEOUT_MS);
  probe.stop();
  if (open) {
    Serial.println("Cloud: Cerbo answers on this WiFi, switching to local");
    if (localFailStreak < 255) localFailStreak++;  // reset to 0 once local works
  }
  return open;
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
      if (shouldSwitchToLocal()) break;
      if (ota_work_due()) break;  // update check or install: needs the memory MQTT's TLS uses
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    bool wifiUp = WiFi.status() == WL_CONNECTED;
    if (mqtt.connected() && wifiUp)
      Serial.printf("%s: closing the MQTT session\n", mode == LinkMode::Cloud ? "Cloud" : "Local");
    else Serial.printf("%s: connection lost%s\n", mode == LinkMode::Cloud ? "Cloud" : "Local",
                       wifiUp ? "" : " (WiFi dropped)");
    mqtt.disconnect();
    if (!wifiUp) WiFi.disconnect(true);
    // SPEC 5.7: with MQTT closed and the WiFi still up, check for or install an update
    if (wifiUp && ota_work_due()) {
      setState(LinkState::Connecting);
      ota_run();  // restarts the board after a successful install
    }
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
  // PubSubClient's disconnect() uses its client without checking, so give it one
  // from the start: a failed Cerbo lookup disconnects before any connect set it
  mqtt.setClient(plainNet);
  mqtt.setBufferSize(1024);
  mqtt.setSocketTimeout(5);
  mqtt.setCallback(onMessage);
  // TLS needs more stack than plain MQTT
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);
}
