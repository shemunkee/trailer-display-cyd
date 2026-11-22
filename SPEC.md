# CYD Victron Display: Build Spec

**Status:** v1 in progress (checklist items 1-4, 6-8 and 12 built; 9 and 10 in part)
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
* MQTT: PubSubClient 2.8 (1 KB buffer), over WiFiClient locally and WiFiClientSecure for cloud. Networking runs in its own FreeRTOS task on core 0; LVGL stays on core 1 and reads status through a mutex.
* Envs: `cyd_ili9341` (default), `cyd_st7789`, `wokwi` (simulator, no touch). Partition table `huge_app.csv` (3 MB app, no OTA).
* NVS namespaces: `settings` (keys `lowsoc`, `snooze`, `bright`, `src`, `lastac`), `touch` (calibration `m` + version `v`), `net` (setup values: `hs_ssid`, `hs_pass`, `mq_host`, `mq_port`, `mq_user`, `gx_pass`, `wf_ssid`, `wf_pass`, `portal`, `vrm_user`, `vrm_token`, `ap_pass` (the setup network password), plus the one-shot `setup` flag set by Run setup). The VRM token is stored bare; `Token ` is added when connecting.

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
  * In `include/venus_ca.h`: "CCGX Certificate Authority", valid to 2114, SHA-256 `94:8B:99:B3:…:11:DF:50:B0`. Both repos had identical copies on 2026-11-22, and `mqtt0` and `mqtt97.victronenergy.com` verified against it (`*.victronenergy.com`, signed directly by this CA).
  * The ESP32 TLS library checks the chain, signature and hostname but not the dates (`MBEDTLS_HAVE_TIME_DATE` is off), so no clock sync is needed.
* Username: VRM email. Password: `Token <access token>`
* The token should come from a separate read only VRM user that only has access to this installation. Note this on the setup page.
* If the broker rejects the login, show "VRM token rejected" in the status. Don't just show blank screens.

### 5.3 Connection logic

1. On boot, try local mode (scan for the hotspot SSID, try to connect for up to about 15s)
2. If that fails, try cloud mode. (Built: each connection attempt scans once, uses the hotspot if it's in range, otherwise the trailer WiFi. Losing the connection starts a new attempt.)
3. While in cloud mode, scan for the hotspot every 2 minutes. If it's found, switch to local. The scan runs without dropping the cloud connection. If the hotspot is in range but local fails (it falls back to cloud), each failure doubles the wait between rescans, up to 30 minutes, so a hotspot without a working broker doesn't keep knocking cloud offline. A working local connection resets it to 2 minutes.
4. If neither works, show the last known values greyed out with a "No connection" status, and keep retrying (built with item 11, see 5.6)
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
| AC input voltage | `vebus/<inst>/Ac/ActiveIn/L1/V` |
| AC out voltage, current, frequency | `vebus/<inst>/Ac/Out/L1/V`, `/I`, `/F` |
| Shore input limit | `vebus/<inst>/Ac/ActiveIn/CurrentLimit` |
| Quattro state | `vebus/<inst>/State` (map to Off, Inverting, Charging, Passthru, etc.) |
| Alarms | `vebus/<inst>/Alarms/*`, `battery/<inst>/Alarms/*`, `solarcharger/<inst>/ErrorCode` (0 off, 1 warning, 2 alarm) |

### 5.6 Stale data

* Never show frozen numbers as if they're live
* **Decision (2026-11-22, for now):** freshness is judged for the whole system, not per group. Data is fresh while the link is up (loading or live) and any message arrived in the last 30s. Otherwise every data screen (Overview, Battery, Solar, AC) keeps the last known values but shows them in grey, with an amber marker top right:
  * `NO CONNECTION`: the link is down or reconnecting
  * `STALE`: connected, but nothing arrived for 30s
  * `NO DATA`: connected and live, but nothing has ever arrived (for example a wrong portal ID)
* Why not per group, as first planned (grey a group after 30s without an update): the Cerbo only publishes a value when it changes, and the keepalive suppresses republishing (5.4). Steady groups such as shore at 0 W, the Quattro state, or solar at night would go grey while still correct. Battery voltage and current change every second or two, so silence across the whole system means data really stopped. (To confirm in item 5.)
* Rejected alternative: keep the per-group rule and send a full keepalive (no `suppress-republish`) every 30s so everything is resent. It works, but each one republishes every value the Cerbo has, which in cloud mode goes over the trailer's internet every 30s.
* Per-group update times are still kept in the data layer (`updatedMs`), in case we revisit this.
* A device that goes away (empty payload) or a null value shows `--`, not a greyed number.
* Settings and Alarms aren't greyed; Settings shows the connection status itself.

## 6. Screens

320x240 landscape. Mockups are the reference: https://claude.ai/artifact/1YQthzTCZrTkbtBv76tEti

### 6.1 Layout rules (all screens)

* Black background
* 12px padding around the edge, 8px between blocks, 8px inside cards
* Type scale: 10px labels, 12px row text, 14px row values, 18px tile values, larger only for hero numbers (SOC, 24px as in the Overview mockup)
* Built-in LVGL fonts have no `·`, so the middle dot is LVGL's bullet symbol
* Until data arrives a value shows `--`, never a made-up number
* Cards: `#111318`, 8px radius
* Colors: text `#F2F2F5`, muted `#9AA0B4`, divider `#23262F`, solar pink `#F0457E`, AC blue `#7C88F7`, charging green `#3FCF8E`, generator orange `#FF8A3D`, warning amber `#F5A524`, alarm red `#FF5A5F`
* Nothing gets cut off at the edges. Check on the real screen, since the CYD's fonts render differently from the mockups.

### 6.2 Navigation

* Bottom bar, 40px tall, on every main screen: one icon button per screen (Overview, Battery, Solar, AC, Alarms, Settings)
* Active screen: card background behind the icon plus a blue bar on top
* Red dot on the Alarms icon when any alarm or warning is active
* No swipes. Taps only (resistive touch).
* Icons are the mockup's line icons, drawn into 20px alpha bitmaps by `tools/make_icons.py` (`include/nav_icons.h`) and colored at runtime (white active, muted otherwise)
* Screens are built once and shown or hidden, so switching and waking are instant. Values refresh every 500 ms.
* Until item 13, Alarms opens a "Coming soon" page and the Alarms dot only shows low SOC.
* LVGL has a 56 KB heap, the most that fits beside everything else in static RAM (64 KB overflows). The boot log prints how much of it the screens use (`UI: built, LVGL memory N% used`).

### 6.3 Overview

* Left column: AC input tile on top, Solar tile below
* Center: SOC ring, 88px, filling to the SOC like a gauge, with SOC %, battery watts (`+` and green while charging) and voltage inside. Color by state: green charging, normal text discharging, amber at or below the low SOC setting (amber wins). Charging means battery power above 0.
* Right column: AC loads tile on top (watts, Quattro AC out amps below), Quattro state tile below
* **Quattro tile:** Charging (for Bulk, Absorption, Float, Storage, Equalize), Inverting, Passthru, Off, Fault (red), and so on. Below: the input limit while an AC input is live, otherwise the charge phase while charging, otherwise blank.
* **Solar tile:** total solar watts, and kWh today added up over all MPPTs
* **AC input tile:** shows SHORE or GENERATOR, whichever is live, with watts. Generator in orange. When neither is live, show the last one used with "Off · last used" in grey. Save the last used source in NVS so it survives a reboot. Active input Source 1 (Grid) or 3 (Shore) counts as SHORE, with power from `Ac/Grid`; 2 is GENERATOR, with power from `Ac/Genset`. Live shore shows blue with "Connected", generator orange with "Running". Before the first value it shows the saved source with `--`.

### 6.4 Battery

SOC card on the left (big %, bar, Charging/Discharging), rows on the right: Voltage, Current, Power, Time to go ("Charging" while charging), Consumed Ah.

* SOC is 36px; the bar and SOC follow the Overview ring colors (green charging, amber at or below low SOC). "Idle" at exactly 0 W.
* Current and power show a sign and turn green while charging. Time to go is shown as `5h 12m`, `99h+` past 100 hours.

### 6.5 Solar

Total solar at the top. Two cards side by side, Driver and Passenger: watts, PV voltage, state, yield today.

* Cards are matched by CustomName containing "driver" or "passenger" (any case). If a name doesn't match, that card shows the next charger not already shown, titled with its own CustomName, so a renamed MPPT still appears.
* Total solar is 18px and MPPT watts 24px (the mockup's 20px and 22px aren't built-in sizes). A Fault state shows red.

### 6.6 AC / Quattro

Two cards: AC input (labelled with the live or last used source: voltage, power, input limit) and AC out (watts, voltage, current, frequency). Quattro state bar below.

* AC input reads "Connected" (blue) or "Running" (orange, generator) when live, "Off" in grey otherwise, with voltage and power greyed while off. Power comes from `Ac/Grid` or `Ac/Genset` as on Overview.
* AC out watts are the AC loads value, as on Overview
* The state bar shows the full Quattro state (Bulk, Absorption, Float, Inverting, ...), Fault in red

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

* Layout as in the Settings mockup: one card per row, − / + buttons for Low SOC and Brightness, five buttons for Snooze with the chosen one in blue
* Data source shows Local or Cloud (green when live, amber while connecting or failing, "No connection" in grey) with a short status below, such as "Live, 22/24 paths" or "VRM token rejected"
* The Data source menu is a card over a dimmed screen with Run setup (restarts into setup), Recalibrate touch, and Close

All settings saved in NVS.

### 6.9 Setup (first boot, or Run setup)

* The CYD starts its own WiFi network (`Victron_CYD`). Its random password is made once per device on the first setup, kept in NVS, and reused every time setup opens, so a phone that joined before can rejoin on its own. The QR code and password text are still shown each time.
* Screen shows two QR codes: (1) join the setup WiFi (standard `WIFI:T:WPA;S:...;P:...;;` format), (2) open `http://192.168.4.1`. Print the network name, password and address as text too.
* Captive portal so most phones open the page on their own
* The page collects: Cerbo hotspot SSID and password, local broker host/port (prefilled), optional GX username and password, trailer WiFi SSID and password, VRM portal ID, VRM email, VRM token
* Saved secrets are never sent back to the page. Show "Saved" with a Replace option.
* On save: store in NVS, shut down the setup network, restart
* Opens on boot when no WiFi (hotspot or trailer) is saved, or after Run setup (sets an NVS flag and restarts). Run setup is in Settings → Data source.
* When rerunning setup, non-secret values (SSIDs, broker host/port, portal ID, VRM email) are prefilled. Passwords, GX password and token only show "Saved" / Replace; a blank field keeps the saved value.
* A Cancel button (shown only when settings already exist) restarts without saving
* The page checks: at least one WiFi, port 1-65535, WiFi passwords 8+ characters; warns if trailer WiFi is set without the VRM portal ID, email and token. A pasted `Token ` prefix is stripped.

### 6.10 Snooze and logo

* After the snooze time with no touch: flash the logo for 1s, then backlight off
* WiFi and MQTT stay connected while snoozed
* Any tap wakes it and returns to the last screen. A tap during the logo cancels the snooze.
* The waking tap only wakes the screen; it never presses a button underneath
* No snooze on the setup screen (the QR codes need to stay visible) or in the Wokwi build (no touch to wake it)
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
- [x] Cloud MQTT: broker index, TLS, token login, error status (still to check on the device; can be tested at home by entering home WiFi as trailer WiFi)
- [x] Local first with cloud fallback, and switching back to local (still to check at the trailer: on cloud, turn the hotspot on and it switches within about 2 minutes)
- [x] Overview screen, including Shore/Generator last used (still to check on the device with live data)
- [ ] Battery, Solar, AC, Alarms, Settings screens (Battery, Solar, AC and Settings built, still to check on the device; Alarms comes with item 13)
- [ ] Bottom nav and alarm dot (nav built; the dot shows low SOC only until item 13)
- [x] Stale data handling (whole-system freshness, see 5.6; still to check on the device)
- [x] Snooze with logo (still to check on the device)
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
| 2026-11-22 | Item 12 built: snooze (`src/snooze.cpp`). Logo now flashes for 1s instead of 3s before the backlight turns off. The logo is drawn by LVGL from flash on the top layer, and also catches the waking tap so nothing underneath is pressed. Uses LVGL's inactivity timer and the Snooze setting, so changes take effect immediately. `snooze_wake()` is ready for alarms (item 13). Flash use 48%. |
| 2026-11-22 | Item 6 built: cloud MQTT. `src/mqtt_local.*` became `src/mqtt_link.*`, one task handling both modes with the same subscriptions, keepalive and logging. Cloud: broker `mqttN.victronenergy.com:8883` from the portal ID, TLS verified with the Victron CA (`include/venus_ca.h`), login VRM email + `Token <token>`, "Cloud: VRM token rejected" on MQTT refusal (4/5), TLS errors logged. Email and token masked in logs. Interim local/cloud choice per attempt (see 5.3); switching back while on cloud is item 7. Flash use 51%. |
| 2026-11-22 | Added OTA updates from GitHub Releases, with on-screen approval and signed firmware, to Later. Planned for when Dad's unit is built. |
| 2026-11-22 | Setup network renamed `Victron_CYD` (was `CYD-Setup`). Its password is no longer new each time: it's generated once per device with the hardware RNG, saved as `net/ap_pass` and reused, so it stays out of source. Erasing flash makes a new one. |
| 2026-11-22 | Item 7 built: while on cloud, a background scan every 2 minutes looks for the hotspot and switches to local when it's in range. Failed local attempts with the hotspot visible back the rescan off (2, 4, 8, 16, then 30 min max). |
| 2026-11-22 | Item 8 built, and Settings plus the bottom nav from items 9-10. New data layer (`src/victron_data.*`): the MQTT task parses each 5.5 value into one shared set, with a last-update time per group (battery, solar, AC, Quattro) for item 11; MPPTs are kept by instance and carry their CustomName. Overview, Settings and the nav follow the mockups (details in 6.1-6.3, 6.8). The demo screen is gone; the device boots into Overview. Fonts: Montserrat 24 added for the SOC ring, 40 dropped. Flash use 50%. |
| 2026-11-22 | Battery, Solar and AC / Quattro screens built from the mockups (details in 6.4-6.6). MPPT cards are matched by CustomName, with a fallback for unmatched names. Added AC input voltage (`vebus/<inst>/Ac/ActiveIn/L1/V`) to 5.5 and the subscriptions, unverified like the rest. Montserrat 36 added. LVGL heap 48 KB to 56 KB, with its use logged at boot. Shared row and label helpers moved to `src/ui.cpp`. Flash use 52%, static RAM 37%. |
| 2026-11-22 | Item 11 built, with a change of plan logged in 5.6: freshness is judged for the whole system (link up and any message in the last 30s) instead of per value group, because the Cerbo only publishes on change and steady groups would grey out falsely. The rejected alternative (a full republish every 30s) would cost cloud data. Stale or disconnected data stays on screen in grey with an amber NO CONNECTION / STALE / NO DATA marker. Chosen by Sabrina "for now"; revisit after item 5 if needed. |
