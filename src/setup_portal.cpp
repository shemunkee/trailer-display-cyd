#include "setup_portal.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>

#include "net_config.h"
#include "setup_page.h"
#include "setup_screen.h"

static constexpr const char* AP_SSID = "CYD-Setup";
static constexpr const char* AP_URL = "http://192.168.4.1";
static const IPAddress AP_IP(192, 168, 4, 1);
static constexpr int PASS_LEN = 10;
static constexpr uint32_t RESTART_DELAY_MS = 1500;

static DNSServer dns;
static WebServer server(80);
static NetConfig cfg;
static char apPass[PASS_LEN + 1];
static uint32_t restartAt = 0;

// Lowercase letters and digits without lookalikes (no 0/o, 1/l/i)
static void makePassword() {
  static const char alphabet[] = "abcdefghjkmnpqrstuvwxyz23456789";
  for (int i = 0; i < PASS_LEN; i++) apPass[i] = alphabet[esp_random() % (sizeof(alphabet) - 1)];
  apPass[PASS_LEN] = 0;
}

static String jsonString(const String& s) {
  String out = "\"";
  for (char ch : s) {
    if (ch == '"' || ch == '\\') {
      out += '\\';
      out += ch;
    } else if ((uint8_t)ch < 0x20) {
      char esc[8];
      snprintf(esc, sizeof(esc), "\\u%04x", ch);
      out += esc;
    } else {
      out += ch;
    }
  }
  return out + "\"";
}

static void sendResult(int code, const String& body) {
  String page = RESULT_PAGE;
  page.replace("%s", body);
  server.send(code, "text/html", page);
}

static void handleRoot() { server.send_P(200, "text/html", SETUP_PAGE); }

// Non-secret values to prefill, and only a saved/not-saved flag for secrets
static void handleConfig() {
  String j = "{";
  j += "\"hs_ssid\":" + jsonString(cfg.hotspotSsid);
  j += ",\"mq_host\":" + jsonString(cfg.mqttHost);
  j += ",\"mq_port\":\"" + String(cfg.mqttPort) + "\"";
  j += ",\"mq_user\":" + jsonString(cfg.mqttUser);
  j += ",\"wf_ssid\":" + jsonString(cfg.wifiSsid);
  j += ",\"portal\":" + jsonString(cfg.portalId);
  j += ",\"vrm_user\":" + jsonString(cfg.vrmUser);
  j += ",\"saved\":{";
  j += String("\"hs_pass\":") + (cfg.hotspotPass.isEmpty() ? "false" : "true");
  j += String(",\"gx_pass\":") + (cfg.gxPass.isEmpty() ? "false" : "true");
  j += String(",\"wf_pass\":") + (cfg.wifiPass.isEmpty() ? "false" : "true");
  j += String(",\"vrm_token\":") + (cfg.vrmToken.isEmpty() ? "false" : "true");
  j += "}}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

static String field(const char* name) {
  String v = server.arg(name);
  v.trim();
  return v;
}

// Secrets only change when a new value is typed; blank keeps the saved one
static void takeSecret(const char* name, String& dest) {
  if (!server.hasArg(name)) return;
  String v = server.arg(name);
  if (!v.isEmpty()) dest = v;
}

static void handleSave() {
  NetConfig next = cfg;
  next.hotspotSsid = field("hs_ssid");
  next.mqttHost = field("mq_host");
  next.mqttUser = field("mq_user");
  next.wifiSsid = field("wf_ssid");
  next.portalId = field("portal");
  next.portalId.toLowerCase();
  next.vrmUser = field("vrm_user");
  takeSecret("hs_pass", next.hotspotPass);
  takeSecret("gx_pass", next.gxPass);
  takeSecret("wf_pass", next.wifiPass);
  takeSecret("vrm_token", next.vrmToken);
  next.vrmToken.trim();
  if (next.vrmToken.startsWith("Token ")) next.vrmToken = next.vrmToken.substring(6);

  String errors;
  long port = field("mq_port").toInt();
  if (next.mqttHost.isEmpty()) next.mqttHost = DEFAULT_MQTT_HOST;
  if (port < 1 || port > 65535) errors += "<p class=\"err\">Broker port must be between 1 and 65535.</p>";
  else next.mqttPort = port;
  if (!net_config_has_network(next))
    errors += "<p class=\"err\">Enter the Cerbo hotspot or the trailer WiFi, or both.</p>";
  if (next.hotspotSsid.length() > 32 || next.wifiSsid.length() > 32)
    errors += "<p class=\"err\">WiFi network names can be at most 32 characters.</p>";
  if ((!next.hotspotPass.isEmpty() && next.hotspotPass.length() < 8) ||
      (!next.wifiPass.isEmpty() && next.wifiPass.length() < 8))
    errors += "<p class=\"err\">WiFi passwords need at least 8 characters.</p>";

  if (!errors.isEmpty()) {
    sendResult(400, errors + "<p><a href=\"/\">Back to the form</a></p>");
    return;
  }

  String msg = "<p class=\"ok\">Saved. The display is restarting and will connect on its own. "
               "You can close this page and rejoin your usual WiFi.</p>";
  bool cloudSet = !next.portalId.isEmpty() && !next.vrmUser.isEmpty() && !next.vrmToken.isEmpty();
  if (!next.wifiSsid.isEmpty() && !cloudSet)
    msg += "<p class=\"warn\">Trailer WiFi is set but the VRM portal ID, email or token is missing, "
           "so cloud mode won't work yet.</p>";

  cfg = next;
  net_config_save(cfg);
  Serial.println("Setup: saved");
  net_config_log(cfg);
  sendResult(200, msg);
  setup_screen_status("Saved, restarting");
  restartAt = millis() + RESTART_DELAY_MS;
}

// Every unknown path, including the phones' captive-portal checks
// (/generate_204, /hotspot-detect.html, /connecttest.txt), goes to the form
static void handleRedirect() {
  server.sendHeader("Location", String(AP_URL) + "/", true);
  server.send(302, "text/plain", "");
}

static void cancel() {
  Serial.println("Setup: cancelled, restarting");
  setup_screen_status("Cancelled, restarting");
  restartAt = millis() + 500;
}

void setup_portal_begin(bool canCancel) {
  net_config_load(cfg);

  // Starting the radio first makes esp_random() a true hardware random source
  WiFi.mode(WIFI_AP);
  delay(100);
  makePassword();
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, apPass);
  Serial.printf("Setup: network %s open at %s (password on screen)\n", AP_SSID, AP_URL);

  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", AP_IP);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleRedirect);
  server.begin();

  setup_screen_create(AP_SSID, apPass, AP_URL, canCancel ? cancel : nullptr);
}

void setup_portal_loop() {
  dns.processNextRequest();
  server.handleClient();
  if (restartAt && (int32_t)(millis() - restartAt) >= 0) {
    server.stop();
    dns.stop();
    WiFi.softAPdisconnect(true);
    ESP.restart();
  }
}
