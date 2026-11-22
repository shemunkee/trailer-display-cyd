# CYD Victron Display: Build Spec

**Status:** v1 in progress (checklist items 1-4 built)
**Owner:** Sabrina
**Last updated:** 2026-11-22

> **For Claude Code:** This file is the source of truth for what the device does. Read it before every task. When a decision changes or a feature ships, update this file in the same change: edit the relevant section, tick the checklist, and add a line to the Change log. Don't build anything listed under Later unless asked. If the code and this spec disagree, ask which one is right.

Screen mockups: https://claude.ai/artifact/1YQthzTCZrTkbtBv76tEti

---

## 1. Goal

A small wall display in the RV that shows the Victron system status I normally check on the VRM portal. It's read only. It gets data from the Cerbo GX over its local hotspot first, and falls back to the VRM cloud when the hotspot isn't in range.

## 2. Hardware

| Item | Detail |
|---|---|
| Board | ESP32-2432S028R "Cheap Yellow Display" (CYD) |
| Screen | 2.8" 320x240, landscape. Our board (single USB-C, CH340 USB serial) has an **ILI9341**, confirmed on the device. Other CYD revisions may use ST7789. Chosen by build env: `cyd_ili9341` (default) or `cyd_st7789`. The boot log prints the built driver and the panel's ILI9341 ID read to help pick. |
| Touch | XPT2046 resistive. Needs calibration, saved on the device. |
| Power | USB only for v1 |
| Memory | No PSRAM. Keep fonts, buffers and images lean. |

## 3. Toolchain

* PlatformIO, Arduino framework
* UI: LVGL (v8 or v9, your call, document which in the Change log) on top of TFT_eSPI or LovyanGFX
* MQTT client with TLS support (cloud mode needs TLS on port 8883)
* Settings stored in NVS (Preferences)
* No secrets in source, in `platformio.ini`, or in git
* MQTT: PubSubClient 2.8 (1 KB buffer). Networking runs in its own FreeRTOS task on core 0; LVGL stays on core 1 and reads status through a mutex.
* Envs: `cyd_ili9341` (default), `cyd_st7789`, `wokwi` (simulator, no touch). Partition table `huge_app.csv` (3 MB app, no OTA).
* NVS namespaces: `settings` (keys `lowsoc`, `snooze`, `bright`, `src`, `lastac`), `touch` (calibration `m` + version `v`), `net` (setup values: `hs_ssid`, `hs_pass`, `mq_host`, `mq_port`, `mq_user`, `gx_pass`, `wf_ssid`, `wf_pass`, `portal`, `vrm_user`, `vrm_token`, plus the one-shot `setup` flag set by Run setup). The VRM token is stored bare; `Token ` is added when connecting.

## 4. Victron system

| Device | Notes |
|---|---|
| Cerbo GX | Venus OS, runs the MQTT broker |
| Quattro 48/3000/35 120V | Inverter/charger. Two AC inputs: one Shore, one Generator (set on the Cerbo under Settings → System setup) |
| SmartShunt 500A | Battery monitor, 48V bank |
| MPPT 100/20 48V "Driver" | Solar charger |
| MPPT 100/20 48V "Passenger" | Solar charger |

Match MPPTs by their `CustomName` (Driver, Passenger), never by device instance number, since instances can change.

## 5. Data sources

### 5.1 Local mode (preferred)

* Join the Cerbo's hotspot (SSID and password from setup)
* MQTT broker at the Cerbo's hotspot address, usually `172.24.24.1`. Make host and port settable in setup, with that default.
* Port 1883. Support optional username/password, because the Cerbo's "Weak" security profile requires the GX password. The username is its own optional setup field (blank by default) because the dbus-flashmq README doesn't say which username goes with the GX password; check with MQTT Explorer.
* No internet access in this mode. That's the point of it.

### 5.2 Cloud mode (fallback)

* Join the trailer WiFi (SSID and password from setup)
* Broker: `mqttN.victronenergy.com`, port 8883, TLS
* Work out `N` from the portal ID: sum the ASCII codes of the lowercase portal ID characters, then take `% 128`
* TLS: verify with the Victron CA certificate (`venus-ca.crt` from the victronenergy/dbus-flashmq or dbus-mqtt repo), stored in the firmware. Never turn off certificate checks.
* Username: VRM email. Password: `Token <access token>`
* The token should come from a separate read only VRM user that only has access to this installation. Note this on the setup page.
* If the broker rejects the login, show "VRM token rejected" in the status. Don't just show blank screens.

### 5.3 Connection logic

1. On boot, try local mode (scan for the hotspot SSID, try to connect for up to about 15s)
2. If that fails, try cloud mode
3. While in cloud mode, scan for the hotspot every 2 minutes. If it's found, switch to local.
4. If neither works, show the last known values greyed out with a "No connection" status, and keep retrying
5. Show the current source (Local or Cloud) on the Settings screen Data source row

### 5.4 MQTT details (both modes)

* Topics use the form `N/<portalId>/<service>/<instance>/<path>`, payload is JSON `{"value": ...}`
* Local mode: try to find the portal ID by subscribing to `N/+/system/0/Serial` for 10s. The dbus-flashmq README doesn't document this (it says nothing is published before a keepalive, and to read the ID off the Cerbo or VRM), so if nothing arrives, use the portal ID from setup. If both exist and differ, the discovered one wins and a warning is logged. In cloud mode use the portal ID from setup.
* Keep the data flowing by publishing a keepalive to `R/<portalId>/keepalive` every 30s (the broker's keepalive timeout is 60s). Checked against the dbus-flashmq README on 2026-11-21: the first keepalive after connecting has an empty payload (the Cerbo republishes every subscribed value), later ones send `{"keepalive-options":["suppress-republish"]}`.
* The end of a full republish is marked by `N/<portalId>/full_publish_completed`. Nothing is retained. Payloads are `{"value": ...}`, `{"value": null}` for an invalid value, and an empty payload when a device has gone.
* Subscribe only to the 5.5 paths, never `N/<portalId>/#`, so the full republish stays small.
* **Read only:** never publish to any `W/` topic. Only `R/` keepalive requests are allowed.

### 5.5 Data points

These paths are my best understanding of Venus OS. **Verify every one against the live system with MQTT Explorer before relying on it**, and correct this table if needed.

| Screen value | Topic path (after `N/<portalId>/`) |
|---|---|
| Battery SOC | `system/0/Dc/Battery/Soc` |
| Battery voltage, current, power | `system/0/Dc/Battery/Voltage`, `/Current`, `/Power` |
| Time to go | `system/0/Dc/Battery/TimeToGo` (seconds, null while charging) |
| Consumed Ah | `battery/<inst>/ConsumedAmphours` |
| Total solar | `system/0/Dc/Pv/Power` |
| MPPT name | `solarcharger/<inst>/CustomName` |
| MPPT power, PV voltage | `solarcharger/<inst>/Yield/Power`, `/Pv/V` |
| MPPT state | `solarcharger/<inst>/State` (map codes to Off, Bulk, Absorption, Float, etc.) |
| MPPT yield today | `solarcharger/<inst>/History/Daily/0/Yield` (kWh) |
| Active AC input | `system/0/Ac/ActiveIn/Source` (1 Grid, 2 Generator, 3 Shore, 240 not connected) |
| Shore power | `system/0/Ac/Grid/L1/Power` |
| Generator power | `system/0/Ac/Genset/L1/Power` |
| AC loads | `system/0/Ac/Consumption/L1/Power` |
| AC out voltage, current, frequency | `vebus/<inst>/Ac/Out/L1/V`, `/I`, `/F` |
| Shore input limit | `vebus/<inst>/Ac/ActiveIn/CurrentLimit` |
| Quattro state | `vebus/<inst>/State` (map to Off, Inverting, Charging, Passthru, etc.) |
| Alarms | `vebus/<inst>/Alarms/*`, `battery/<inst>/Alarms/*`, `solarcharger/<inst>/ErrorCode` (0 off, 1 warning, 2 alarm) |

### 5.6 Stale data

* If no update arrives for a value group for 30s, show that group greyed with a small "stale" marker
* Never show frozen numbers as if they're live

## 6. Screens

320x240 landscape. Mockups are the reference: https://claude.ai/artifact/1YQthzTCZrTkbtBv76tEti

### 6.1 Layout rules (all screens)

* Black background
* 12px padding around the edge, 8px between blocks, 8px inside cards
* Type scale: 10px labels, 12px row text, 14px row values, 18px tile values, larger only for hero numbers (SOC)
* Cards: `#111318`, 8px radius
* Colors: text `#F2F2F5`, muted `#9AA0B4`, divider `#23262F`, solar pink `#F0457E`, AC blue `#7C88F7`, charging green `#3FCF8E`, generator orange `#FF8A3D`, warning amber `#F5A524`, alarm red `#FF5A5F`
* Nothing gets cut off at the edges. Check on the real screen, since the CYD's fonts render differently from the mockups.

### 6.2 Navigation

* Bottom bar, 40px tall, on every main screen: one icon button per screen (Overview, Battery, Solar, AC, Alarms, Settings)
* Active screen: card background behind the icon plus a blue bar on top
* Red dot on the Alarms icon when any alarm or warning is active
* No swipes. Taps only (resistive touch).

### 6.3 Overview

* Left column: AC input tile on top, Solar tile below
* Center: SOC ring, color by state (green charging, normal text discharging, amber at or below the low SOC setting)
* Right column: AC loads tile on top, Quattro state tile below
* **AC input tile:** shows SHORE or GENERATOR, whichever is live, with watts. Generator in orange. When neither is live, show the last one used with "Off · last used" in grey. Save the last used source in NVS so it survives a reboot.

### 6.4 Battery

SOC card on the left (big %, bar, Charging/Discharging), rows on the right: Voltage, Current, Power, Time to go ("Charging" while charging), Consumed Ah.

### 6.5 Solar

Total solar at the top. Two cards side by side, Driver and Passenger: watts, PV voltage, state, yield today.

### 6.6 AC / Quattro

Two cards: AC input (labelled with the live or last used source: voltage, power, input limit) and AC out (watts, voltage, current, frequency). Quattro state bar below.

### 6.7 Alarms

List of active alarms and warnings: level (ALARM red, WARNING amber), message, device, since time. Footer: "Read only. Clear alarms on the Cerbo." Empty state: "No active alarms".

### 6.8 Settings

Four rows:

| Row | Options | Default |
|---|---|---|
| Low SOC warning | Steps of 5% | 30% |
| Snooze | 5s, 10s, 30s, 60s, 90s | 30s |
| Brightness | Steps of 10% | 70% |
| Data source | Shows Local or Cloud and status. Tap opens: Run setup, Recalibrate touch | Auto |

All settings saved in NVS.

### 6.9 Setup (first boot, or Run setup)

* The CYD starts its own WiFi network (`CYD-Setup`) with a random password made fresh each time setup opens
* Screen shows two QR codes: (1) join the setup WiFi (standard `WIFI:T:WPA;S:...;P:...;;` format), (2) open `http://192.168.4.1`. Print the network name, password and address as text too.
* Captive portal so most phones open the page on their own
* The page collects: Cerbo hotspot SSID and password, local broker host/port (prefilled), optional GX username and password, trailer WiFi SSID and password, VRM portal ID, VRM email, VRM token
* Saved secrets are never sent back to the page. Show "Saved" with a Replace option.
* On save: store in NVS, shut down the setup network, restart
* Opens on boot when no WiFi (hotspot or trailer) is saved, or after Run setup (sets an NVS flag and restarts). Until the Settings screen exists, the demo screen has a Run setup button.
* When rerunning setup, non-secret values (SSIDs, broker host/port, portal ID, VRM email) are prefilled. Passwords, GX password and token only show "Saved" / Replace; a blank field keeps the saved value.
* A Cancel button (shown only when settings already exist) restarts without saving
* The page checks: at least one WiFi, port 1-65535, WiFi passwords 8+ characters; warns if trailer WiFi is set without the VRM portal ID, email and token. A pasted `Token ` prefix is stripped.

### 6.10 Snooze and logo

* After the snooze time with no touch: show the logo for 3s, then backlight off
* WiFi and MQTT stay connected while snoozed
* Any tap wakes it and returns to the last screen. A tap during the logo cancels the snooze.
* Logo: `include/logo_rgb565.h`, 320x240 RGB565 on black, in PROGMEM

### 6.11 Alerts

* SOC at or below the low SOC setting: SOC turns amber and the Alarms dot shows
* Any device alarm: wake the screen and go to Alarms
* No sounds in v1

## 7. Security and privacy rules

* Read only. Never publish to `W/` topics.
* No credentials, tokens, SSIDs or portal IDs in the source code or git. Everything comes from setup and lives in NVS.
* Never print secrets to serial logs. Mask them (for example `Token ****abcd`). Passwords log only as set/empty; the setup WiFi password is shown on screen, never logged.
* NVS is not encrypted in v1, so someone with the board in hand could read the saved values. Turning on ESP32 flash encryption is hard to undo, so it's left out; the read-only VRM user limits the damage.
* Cloud mode always verifies TLS certificates
* No telemetry, no other servers, no OTA updates in v1
* No notifications, emails or messages sent from the device

## 8. Later (don't build yet)

* Control functions (Quattro mode, input current limit), with confirm dialogs, off by default
* Tank and temperature screens
* Battery power and charging
* Wake on pickup (motion sensor)
* History graphs (VRM API or local)
* Port to a larger display, sharing the same data layer
* OTA updates from GitHub Releases (plan for when a second unit is built for Dad):
    * Display checks a GitHub Release for a newer version about once a day, only when it has internet (Cloud mode or home WiFi). Settings gets a "Check for updates" button.
    * Shows "Update available, tap to install". Never installs without a tap on the device.
    * Firmware is signed with a private key kept on Sabrina's laptop and never committed. The device rejects anything not signed with it.
    * HTTPS with certificate checks. Rollback to the previous version if the new one fails to boot.
    * Needs a two slot (OTA) partition layout instead of `huge_app.csv`, which means one last USB flash to switch over.
    * First flash for a new unit: browser flasher (ESP Web Tools) hosted on GitHub Pages.
    * Each unit uses its own read only VRM user and token for its own installation.

## 9. Build checklist

- [x] Project scaffold, display and touch working, touch calibration saved (confirmed on the device with `cyd_ili9341`, including calibration kept after a power cycle)
- [x] Settings in NVS (confirmed on the device: setting changed on the demo screen kept after a power cycle)
- [x] Setup portal with QR codes (confirmed on the device: QR scan, captive page, save and restart, rerun shows Saved, Cancel)
- [x] Local MQTT: connect, portal ID discovery, keepalive (still to check at the trailer: join, connect, portal ID, paths seen, keepalive every 30s)
- [ ] Topic paths verified against the live system (update 5.5)
- [ ] Cloud MQTT: broker index, TLS, token login, error status
- [ ] Local first with cloud fallback, and switching back to local
- [ ] Overview screen, including Shore/Generator last used
- [ ] Battery, Solar, AC, Alarms, Settings screens
- [ ] Bottom nav and alarm dot
- [ ] Stale data handling
- [ ] Snooze with logo
- [ ] Low SOC and alarm alerts
- [ ] On device check: nothing cut off, all text readable

## 10. Change log

| Date | Change |
|---|---|
| 2026-11-21 | v1 spec written from design sessions |
| 2026-11-21 | Items 1-2 built. UI: LVGL 9.2.2 on TFT_eSPI 2.5.43 (own flush callback, 20-line partial buffer, 48 KB LVGL heap, Montserrat 10/12/14/18/40 only). Display driver is a build flag (`cyd_st7789` default, `cyd_ili9341`); pins and touch bus reused from the old victron-cyd project. Partition `huge_app.csv`. Touch calibration: 3-point affine fit plus a 4th check point (max 8 px error), saved in NVS `touch`; runs on first boot or when the screen is held 3 s at power-up; `calibration_run()` is ready for Settings → Recalibrate touch. Settings in NVS `settings`, range-checked on load, written only on change. A temporary demo screen tests both items until the real screens land. |
| 2026-11-21 | Our board is ILI9341 (confirmed: display works and touch calibration ran with `cyd_ili9341`). Made `cyd_ili9341` the default env; `cyd_st7789` kept for other revisions. |
| 2026-11-21 | Item 3 built: setup portal. `CYD-Setup` WPA2 network with a fresh 10-character password (hardware RNG, no lookalike characters), captive DNS that sends every lookup and unknown path to the form, LVGL QR codes (`LV_USE_QRCODE`, no extra libraries; Arduino WebServer and DNSServer). Values saved to NVS `net`. Added the setup trigger, prefill and Cancel behavior to 6.9, and the unencrypted NVS note to 7. Shared LVGL helpers moved to `src/ui.cpp`. Flash use is now 43% of the 3 MB app slot, RAM 34%. |
| 2026-11-21 | Setup portal confirmed on the device (`cyd_ili9341`): QR join, captive page, save and restart with masked log, rerun prefill and Saved/Replace, Cancel. |
| 2026-11-21 | Items 1-2 fully confirmed on the device: touch calibration and demo screen settings survive a power cycle. |
| 2026-11-22 | Item 4 built: local MQTT (`src/mqtt_local.cpp`). Scans for and joins the hotspot (15s), connects with optional GX username/password, finds the portal ID (discovery first, setup ID fallback), subscribes to the 5.5 paths plus `full_publish_completed`, keepalive as above. A single publish function refuses anything not starting with `R/`. Serial log prints the first value seen per 5.5 path and lists missing paths after each full publish, to help with item 5; portal IDs are masked in logs. Status shows on the demo screen. Added the optional GX username (`mq_user`) to setup. Updated 5.1 and 5.4 from the dbus-flashmq README. |
| 2026-11-22 | Added OTA updates from GitHub Releases, with on-screen approval and signed firmware, to Later. Planned for when Dad's unit is built. |
