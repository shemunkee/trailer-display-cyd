#include "ota.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <string.h>

#include <memory>

#include "ota_ca.h"
#include "ota_pubkey.h"
#include "version.h"

#ifndef FW_BUILD
#define FW_BUILD "unknown"
#endif

static constexpr uint32_t FIRST_CHECK_MS = 60000;            // after boot, once connected
static constexpr uint32_t CHECK_EVERY_MS = 24UL * 3600000;   // SPEC 5.7: about once a day
static constexpr uint32_t RETRY_MS = 6UL * 3600000;          // after a failed check
static constexpr uint32_t KEEP_AFTER_MS = 60000;             // new version must run this long
static constexpr uint32_t HTTP_TIMEOUT_MS = 20000;
static constexpr size_t MANIFEST_MAX = 2048;
static constexpr size_t SIG_MAX = 160;

static OtaStatus status;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool checkAsked = false, installAsked = false;
static uint32_t nextCheckMs = FIRST_CHECK_MS;
static bool keepPending = false;  // running a new version that isn't confirmed yet

// What the last good check found for this board
static char binFile[48];
static uint32_t binSize = 0;
static char binSha[65];

// ---------- status ----------

template <typename F>
static void edit(F f) {
  portENTER_CRITICAL(&mux);
  f(status);
  portEXIT_CRITICAL(&mux);
}

static void setState(OtaState s, const char* msg = "") {
  edit([&](OtaStatus& st) {
    st.state = s;
    strlcpy(st.message, msg, sizeof(st.message));
  });
}

OtaStatus ota_status() {
  OtaStatus s;
  portENTER_CRITICAL(&mux);
  s = status;
  portEXIT_CRITICAL(&mux);
  return s;
}

const char* ota_version() { return FW_VERSION; }

static bool enabled() { return OTA_REPO[0] != 0; }

// ---------- boot and rollback ----------

// The Arduino core asks this at startup. true = don't confirm the running
// firmware yet; ota_loop() does it after KEEP_AFTER_MS. If the board resets
// before then, the bootloader goes back to the previous version.
extern "C" bool verifyRollbackLater() { return true; }

void ota_begin() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  keepPending = esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  bool rolledBack = esp_ota_get_last_invalid_partition() != nullptr;
  Serial.printf("OTA: firmware %s (%s) in %s%s%s\n", FW_VERSION, FW_BUILD, running->label,
                keepPending ? ", new, kept after 60 s" : "", enabled() ? "" : ", update checks off (OTA_REPO not set)");
  if (rolledBack) Serial.println("OTA: an earlier update didn't start properly and was rolled back");
  edit([&](OtaStatus& s) {
    s.state = enabled() ? OtaState::Idle : OtaState::Off;
    s.rolledBack = rolledBack;
  });
}

void ota_loop() {
  if (keepPending && millis() > KEEP_AFTER_MS) {
    keepPending = false;
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("OTA: new firmware ran 60 s, keeping it");
  }
}

// ---------- requests ----------

void ota_request_check() {
  if (enabled()) checkAsked = true;
}

void ota_request_install() {
  OtaState s = ota_status().state;
  if (s == OtaState::Available || s == OtaState::InstallFailed) installAsked = true;
}

bool ota_work_due() {
  if (!enabled()) return false;
  return installAsked || checkAsked || (int32_t)(millis() - nextCheckMs) >= 0;
}

// ---------- helpers ----------

// "1.10.2" > "1.9.9"
static bool newer(const char* a, const char* b) {
  int x[3] = {}, y[3] = {};
  sscanf(a, "%d.%d.%d", &x[0], &x[1], &x[2]);
  sscanf(b, "%d.%d.%d", &y[0], &y[1], &y[2]);
  for (int i = 0; i < 3; i++)
    if (x[i] != y[i]) return x[i] > y[i];
  return false;
}

static String releaseUrl(const char* path) { return String("https://github.com/") + OTA_REPO + "/releases/" + path; }

// GitHub answers with a redirect to its file host; both are checked against OTA_CA
static bool open(HTTPClient& http, WiFiClientSecure& tls, const String& url, char* err, size_t errLen) {
  tls.setCACert(OTA_CA);
  tls.setHandshakeTimeout(15);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setUserAgent("cyd-victron-display");
  if (!http.begin(tls, url)) {
    snprintf(err, errLen, "Couldn't start HTTPS");
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    if (code < 0) {
      char tlsErr[64] = "";
      tls.lastError(tlsErr, sizeof(tlsErr));
      snprintf(err, errLen, "No connection (%d)", code);
      Serial.printf("OTA: %s failed: %s %s\n", url.c_str(), http.errorToString(code).c_str(), tlsErr);
    } else {
      snprintf(err, errLen, code == 404 ? "No release found" : "Server said %d", code);
      Serial.printf("OTA: %s answered %d\n", url.c_str(), code);
    }
    http.end();
    return false;
  }
  return true;
}

// Small file into a buffer
static bool fetch(const String& url, uint8_t* buf, size_t cap, size_t& len, char* err, size_t errLen) {
  WiFiClientSecure tls;
  HTTPClient http;
  if (!open(http, tls, url, err, errLen)) return false;
  int size = http.getSize();
  if (size > (int)cap) {
    snprintf(err, errLen, "File too big");
    http.end();
    return false;
  }
  auto* s = http.getStreamPtr();
  len = 0;
  uint32_t last = millis();
  while ((size < 0 || len < (size_t)size) && len < cap && millis() - last < HTTP_TIMEOUT_MS) {
    int n = s->available();
    if (n > 0) {
      len += s->readBytes(buf + len, min((size_t)n, cap - len));
      last = millis();
    } else if (!http.connected()) {
      break;
    } else {
      delay(5);
    }
  }
  http.end();
  if (size >= 0 && len != (size_t)size) {
    snprintf(err, errLen, "Download cut short");
    return false;
  }
  return true;
}

static bool signatureOk(const uint8_t* data, size_t len, const uint8_t* sig, size_t sigLen) {
  uint8_t hash[32];
  mbedtls_sha256(data, len, hash, 0);
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  int rc = mbedtls_pk_parse_public_key(&pk, (const unsigned char*)OTA_PUBKEY, sizeof(OTA_PUBKEY));
  if (rc == 0) rc = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), sig, sigLen);
  mbedtls_pk_free(&pk);
  return rc == 0;
}

// Value of "key":"..." (string) or "key":123 (number) after `from`, before `end`
static bool jsonValue(const char* from, const char* end, const char* key, char* out, size_t outLen) {
  char pat[40];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char* p = strstr(from, pat);
  if (!p || p >= end) return false;
  p += strlen(pat);
  while (*p == ' ') p++;
  bool str = *p == '"';
  if (str) p++;
  size_t n = 0;
  while (*p && p < end && n < outLen - 1 && (str ? *p != '"' : (*p != ',' && *p != '}' && *p != ' '))) out[n++] = *p++;
  out[n] = 0;
  return n > 0;
}

// ---------- check ----------

static void check() {
  setState(OtaState::Checking);
  Serial.printf("OTA: checking for updates (heap %u free, largest block %u)\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
  // On the heap, only while checking: static RAM is full (LVGL has most of it)
  std::unique_ptr<uint8_t[]> manifestBuf(new uint8_t[MANIFEST_MAX + 1]);
  uint8_t* manifest = manifestBuf.get();
  uint8_t sig[SIG_MAX];
  size_t mLen = 0, sLen = 0;
  char err[48];
  bool ok = fetch(releaseUrl("latest/download/manifest.json"), manifest, MANIFEST_MAX, mLen, err, sizeof(err)) &&
            fetch(releaseUrl("latest/download/manifest.sig"), sig, SIG_MAX, sLen, err, sizeof(err));
  if (ok && !signatureOk(manifest, mLen, sig, sLen)) {
    ok = false;
    snprintf(err, sizeof(err), "Release not signed with our key");
  }
  manifest[mLen] = 0;
  char version[16] = "", size[16] = "";
  const char* m = (const char*)manifest;
  const char* build = ok ? strstr(m, "\"" FW_BUILD "\":{") : nullptr;
  const char* buildEnd = build ? strchr(build, '}') : nullptr;
  if (ok && !jsonValue(m, m + mLen, "version", version, sizeof(version))) {
    ok = false;
    snprintf(err, sizeof(err), "Manifest unreadable");
  }
  if (ok && newer(version, FW_VERSION)) {
    if (!buildEnd || !jsonValue(build, buildEnd, "file", binFile, sizeof(binFile)) ||
        !jsonValue(build, buildEnd, "size", size, sizeof(size)) ||
        !jsonValue(build, buildEnd, "sha256", binSha, sizeof(binSha)) || strlen(binSha) != 64) {
      ok = false;
      snprintf(err, sizeof(err), "No %s build in %s", FW_BUILD, version);
    }
    binSize = strtoul(size, nullptr, 10);
  }

  nextCheckMs = millis() + (ok ? CHECK_EVERY_MS : RETRY_MS);
  edit([](OtaStatus& st) { st.checksDone++; });
  if (!ok) {
    Serial.printf("OTA: check failed: %s\n", err);
    setState(OtaState::CheckFailed, err);
    return;
  }
  if (newer(version, FW_VERSION)) {
    Serial.printf("OTA: version %s available (%s, %u bytes), signature checked\n", version, binFile, (unsigned)binSize);
    edit([&](OtaStatus& s) { strlcpy(s.latest, version, sizeof(s.latest)); });
    setState(OtaState::Available);
  } else {
    Serial.printf("OTA: up to date (latest release %s)\n", version);
    setState(OtaState::UpToDate);
  }
}

// ---------- install ----------

static void installFailed(const char* why) {
  Serial.printf("OTA: install failed: %s\n", why);
  setState(OtaState::InstallFailed, why);
}

static void install() {
  OtaStatus st = ota_status();
  edit([](OtaStatus& s) { s.progress = 0; });
  setState(OtaState::Installing);
  Serial.printf("OTA: installing %s\n", st.latest);

  WiFiClientSecure tls;
  HTTPClient http;
  char err[48];
  if (!open(http, tls, releaseUrl((String("download/v") + st.latest + "/" + binFile).c_str()), err, sizeof(err))) {
    installFailed(err);
    return;
  }
  if (http.getSize() != (int)binSize) {
    http.end();
    installFailed("File size doesn't match the manifest");
    return;
  }
  if (!Update.begin(binSize, U_FLASH)) {
    http.end();
    installFailed(Update.errorString());
    return;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  constexpr size_t BUF = 2048;
  std::unique_ptr<uint8_t[]> bufHolder(new uint8_t[BUF]);  // heap, only while installing
  uint8_t* buf = bufHolder.get();
  auto* s = http.getStreamPtr();
  uint32_t written = 0, last = millis();
  bool ok = true;
  while (written < binSize) {
    int n = s->available();
    if (n <= 0) {
      if (!http.connected() || millis() - last > HTTP_TIMEOUT_MS) {
        ok = false;
        break;
      }
      delay(2);
      continue;
    }
    size_t got = s->readBytes(buf, min((size_t)n, min(BUF, (size_t)(binSize - written))));
    if (Update.write(buf, got) != got) {
      ok = false;
      break;
    }
    mbedtls_sha256_update(&sha, buf, got);
    written += got;
    last = millis();
    uint8_t pct = written * 100ULL / binSize;
    edit([&](OtaStatus& o) { o.progress = pct; });
  }
  http.end();
  uint8_t hash[32];
  mbedtls_sha256_finish(&sha, hash);
  mbedtls_sha256_free(&sha);

  if (!ok) {
    Update.abort();
    installFailed(Update.hasError() ? Update.errorString() : "Download stopped");
    return;
  }
  char hex[65];
  for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", hash[i]);
  if (strcasecmp(hex, binSha) != 0) {
    Update.abort();
    installFailed("Download doesn't match the signed manifest");
    return;
  }
  if (!Update.end()) {
    installFailed(Update.errorString());
    return;
  }
  Serial.printf("OTA: %s written and verified, restarting\n", st.latest);
  setState(OtaState::Restarting);
  delay(1500);
  ESP.restart();
}

void ota_run() {
  if (installAsked) {
    installAsked = false;
    install();
  } else {
    checkAsked = false;
    check();
  }
}
