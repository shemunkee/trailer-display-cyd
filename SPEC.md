# CYD Victron Display: Build Spec

**Status:** v1 in progress (checklist items 1-13 built, 5 confirmed on the live system; 14 is the on-device check). Tanks (LPG) built in 1.0.4, to check on the device.
**Owner:** Sabrina
**Last updated:** 2026-11-25

> **For Claude Code:** This file is the source of truth for what the device does. Read it before every task. When a decision changes or a feature ships, update this file in the same change: edit the relevant section, tick the checklist, and add a line to the Change log. Don't build anything listed under Later unless asked. If the code and this spec disagree, ask which one is right.

Screen mockups: `design/screens.html` (open in a browser; real 320 x 240 size). Sources in `design/src`: edit the generators, then run `python design/src/build_screens.py`. Older canvas copy: https://claude.ai/artifact/1YQthzTCZrTkbtBv76tEti

---

## 1. Goal

A small wall display in the RV that shows the Victron system status I normally check on the VRM portal. It's read only. It joins the trailer WiFi (the same network the Cerbo GX is on), reads the Cerbo directly first, and falls back to the VRM cloud over that WiFi when the Cerbo can't be reached.

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
* Envs: `cyd_ili9341` (default), `cyd_st7789`, `wokwi` (simulator, no touch). Partition table `partitions_ota.csv`: two 1.94 MB firmware slots for OTA (5.7); `nvs` and `otadata` sit where `huge_app.csv` had them, so settings survive the switch.
* NVS namespaces: `settings` (keys `lowsoc`, `snooze`, `bright`, `src`, `lastac`, `flip`), `touch` (calibration `m` + version `v`), `net` (setup values: `name` (display name), `mq_host`, `mq_port`, `mq_user`, `gx_pass`, `wf_ssid`, `wf_pass` (the one WiFi), `portal`, `vrm_user`, `vrm_token`, `ap_pass` (the setup network password), plus the one-shot `setup` flag set by Run setup). Older `hs_ssid` / `hs_pass` (Cerbo hotspot) are read once as the WiFi if none is saved, and removed on the next save; a saved broker `172.24.24.1` becomes `venus.local`. The VRM token is stored bare; `Token ` is added when connecting.

## 4. Victron system

| Device | Notes |
|---|---|
| Cerbo GX | Venus OS, runs the MQTT broker |
| Quattro 48/3000/35 120V | Inverter/charger. Two AC inputs: one Shore, one Generator (set on the Cerbo under Settings → System setup) |
| SmartShunt 500A | Battery monitor, 48V bank |
| MPPT 100/20 48V "Driver" | Solar charger |
| MPPT 100/20 48V "Passenger" | Solar charger |
| 2 x Mopeka Pro LPG sensors | Propane bottles, reach the Cerbo over Bluetooth and appear as `tank` services. Sabrina's are named "Driver Side Tank" and "Passenger Side Tank" (20 lb bottles); Dad's unit also has two (40 lb bottles). |

Match MPPTs by their `CustomName` (Driver, Passenger), never by device instance number, since instances can change. Tanks too: shown by name, sorted, never by instance.

## 5. Data sources

### 5.1 Local mode (preferred)

* **Decision (2026-11-22):** one WiFi only. The Cerbo and the display both join the trailer WiFi, so there's no separate Cerbo hotspot setting. (Was: join the Cerbo's own hotspot at `172.24.24.1`, with the trailer WiFi only for cloud.)
* MQTT broker on the Cerbo, address settable in setup with default `venus.local` (the name the Cerbo announces over mDNS). An IP address or a normal host name also works. MQTT has to be switched on in the Cerbo's settings.
* Port 1883. Support optional username/password, because the Cerbo's "Weak" security profile requires the GX password. The username is its own optional setup field (blank by default) because the dbus-flashmq README doesn't say which username goes with the GX password; check with MQTT Explorer.
* Local mode needs no internet, only the WiFi. That's the point of it.

### 5.2 Cloud mode (fallback)

* Over the same trailer WiFi, when it has internet
* Broker: `mqttN.victronenergy.com`, port 8883, TLS
* Work out `N` from the portal ID: sum the ASCII codes of the lowercase portal ID characters, then take `% 128`
* TLS: verify with the Victron CA certificate (`venus-ca.crt` from the victronenergy/dbus-flashmq or dbus-mqtt repo), stored in the firmware. Never turn off certificate checks.
  * In `include/venus_ca.h`: "CCGX Certificate Authority", valid to 2114, SHA-256 `94:8B:99:B3:…:11:DF:50:B0`. Both repos had identical copies on 2026-11-22, and `mqtt0` and `mqtt97.victronenergy.com` verified against it (`*.victronenergy.com`, signed directly by this CA).
  * The ESP32 TLS library checks the chain, signature and hostname but not the dates (`MBEDTLS_HAVE_TIME_DATE` is off), so no clock sync is needed.
* Username: VRM email. Password: `Token <access token>`
* The token should come from a separate read only VRM user that only has access to this installation. Note this on the setup page.
* If the broker rejects the login, show "VRM token rejected" in the status. Don't just show blank screens.

### 5.3 Connection logic

1. On boot, join the WiFi (scan for it, try to join for up to about 15s), then try local mode: look up the Cerbo address (mDNS for `.local` names, 2s) and connect to its broker
2. If local fails, try cloud mode over the same WiFi, without rejoining. Losing the connection starts a new attempt; the WiFi is only rejoined if it dropped.
3. While in cloud mode, every 2 minutes check whether the Cerbo's broker answers (look up the address, open and close a connection to the port, up to 3s), without dropping the cloud connection. If it answers, switch to local. If it answers but local then fails (it falls back to cloud), the wait doubles each time, up to 30 minutes, so a Cerbo that refuses our login doesn't keep knocking cloud offline. A working local connection resets it to 2 minutes.
4. If neither works, show the last known values greyed out with a "No connection" status, and keep retrying (built with item 11, see 5.6)
5. Show the current source (Local or Cloud) on the Settings screen Data source row

### 5.4 MQTT details (both modes)

* Topics use the form `N/<portalId>/<service>/<instance>/<path>`, payload is JSON `{"value": ...}`
* Local mode: try to find the portal ID by subscribing to `N/+/system/0/Serial` for 10s. The dbus-flashmq README doesn't document this (it says nothing is published before a keepalive, and to read the ID off the Cerbo or VRM), so if nothing arrives, use the portal ID from setup. If both exist and differ, it's another trailer's Cerbo on the same WiFi (venus.local can find either): local mode refuses it (status "Different Cerbo found (portal ID)") and falls back to cloud; the log suggests entering our Cerbo by IP. (Changed 2026-11-22; was: the discovered one wins.) In cloud mode use the portal ID from setup.
* Keep the data flowing by publishing a keepalive to `R/<portalId>/keepalive` every 30s (the broker's keepalive timeout is 60s). Checked against the dbus-flashmq README on 2026-11-21: the first keepalive after connecting has an empty payload (the Cerbo republishes every subscribed value), later ones send `{"keepalive-options":["suppress-republish"]}`.
* The end of a full republish is marked by `N/<portalId>/full_publish_completed`. Nothing is retained. Payloads are `{"value": ...}`, `{"value": null}` for an invalid value, and an empty payload when a device has gone.
* Subscribe only to the 5.5 paths, never `N/<portalId>/#`, so the full republish stays small.
* **Read only:** never publish to any `W/` topic. Only `R/` keepalive requests are allowed.

### 5.5 Data points

These paths are my best understanding of Venus OS. **Verify every one against the live system with MQTT Explorer before relying on it**, and correct this table if needed.

**Checked 2026-11-22 over cloud:** all 25 subscribed paths delivered data and the full publish completed. Seen on the live system: Quattro is `vebus/276`, SmartShunt `battery/279`, the Driver MPPT `solarcharger/277` (instances can change, so screens still match by name). Notes from the values: `Dc/Pv/Power` reads slightly negative at night (-5.7 W), so solar power is clamped to 0; `TimeToGo` is about 864000 s (240 h) when full and idle, shown as 99h+; `CurrentLimit` also carries `min` and `max` next to `value`; `Ac/Genset/L1/Power` is null with no generator, shown as `--`. Battery alarm names seen: `Alarms/Alarm`; Quattro: `Alarms/TemperatureSensor` (others arrive under the same wildcard). The Passenger MPPT reports too (both cards showed 0 W at night).

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
| Tank level | `tank/<inst>/Level` (0 to 100 %) |
| Tank propane left | `tank/<inst>/Remaining` (m³); shown in lb = m³ × 264.172 × 4.24, one decimal |
| Tank capacity | `tank/<inst>/Capacity` (m³, set per tank on the Cerbo) |
| Tank name | `tank/<inst>/CustomName` |
| Tank fluid | `tank/<inst>/FluidType` (8 = LPG; only LPG tanks are shown) |
| Tank sensor status | `tank/<inst>/Status` (0 Ok, 1 Disconnected, 2 Short circuited, 3 Unknown, 4 Configuration error) |
| Alarms | `vebus/<inst>/Alarms/*`, `battery/<inst>/Alarms/*` (0 off, 1 warning, 2 alarm; the Quattro also has `Alarms/L1/*` copies, merged into one entry). `solarcharger/<inst>/ErrorCode` is a Victron error number instead (0 none), so any non-zero code counts as an alarm. |

**Tank paths checked against the Venus dbus wiki (github.com/victronenergy/venus/wiki/dbus, "Tank levels", 2026-11-25):** Level "0 to 100%", Remaining "m3", Capacity "m3"; FluidType 0 Fuel, 1 Fresh water, 2 Waste water, 3 Live well, 4 Oil, 5 Black water, 6 Gasoline, 7 Diesel, **8 LPG**, 9 LNG, 10 Hydraulic oil, 11 Raw water (kept in sync with NMEA2000); Status 0 Ok, 1 Disconnected, 2 Short circuited, 3 Unknown, 4 Configuration error. `CustomName` isn't listed in the wiki's tank section (it's a general path on Venus services), and the dbus-flashmq README doesn't mention tanks; it only gives the topic form `N/<portal ID>/<service type>/<device instance>/<D-Bus path>`, so `N/<portalId>/tank/<inst>/...`. All six are subscribed with a one-level `+` for the instance (never `#`), 31 paths in all; the boot log's "seen" lines will confirm them on the live system. Not used: tank alarms (`Alarms/Low|High/*`, not supported for every tank type), Mopeka battery voltage and temperature.

### 5.6 Stale data

* Never show frozen numbers as if they're live
* **Decision (2026-11-22, for now):** freshness is judged for the whole system, not per group. Data is fresh while the link is up (loading or live) and any message arrived in the last 30s. Otherwise every data screen (Overview, Battery, Solar, AC, Tanks, Alarms) keeps the last known values but shows them in grey, with an amber marker top right:
  * `NO CONNECTION`: the link is down or reconnecting
  * `STALE`: connected, but nothing arrived for 30s
  * `NO DATA`: connected and live, but nothing has ever arrived (for example a wrong portal ID)
* Why not per group, as first planned (grey a group after 30s without an update): the Cerbo only publishes a value when it changes, and the keepalive suppresses republishing (5.4). Steady groups such as shore at 0 W, the Quattro state, or solar at night would go grey while still correct. Battery voltage and current change every second or two, so silence across the whole system means data really stopped. (To confirm in item 5.)
* Rejected alternative: keep the per-group rule and send a full keepalive (no `suppress-republish`) every 30s so everything is resent. It works, but each one republishes every value the Cerbo has, which in cloud mode goes over the trailer's internet every 30s.
* Per-group update times are still kept in the data layer (`updatedMs`: battery, solar, AC, Quattro, tanks), in case we revisit this. Tanks have their own group, and the Tanks screen and Overview strip grey out with the rest.
* A device that goes away (empty payload) or a null value shows `--`, not a greyed number.
* Settings isn't greyed; it shows the connection status itself. Alarms is greyed too, since the list could be out of date.

### 5.7 Firmware updates (OTA)

* **Decision (2026-11-22):** built now, ahead of Dad's unit (was in Later). Sabrina: "lets do ota updates now".
* Releases live on GitHub Releases of a **public** repo: `shemunkee/trailer-display-cyd` (`OTA_REPO` in `include/version.h`; GitHub ignores case, and 1.0.0 to 1.0.3 used `SheMunkee`). Empty `OTA_REPO` = update checks off. The device never logs in to GitHub.
* Each release has `firmware-cyd_ili9341.bin`, `firmware-cyd_st7789.bin`, `manifest.json` and `manifest.sig`. The manifest holds the version plus each file's size and SHA-256; each board fetches the file for its own build (`FW_BUILD`).
* **Signing:** `manifest.json` is signed (ECDSA P-256, SHA-256) with a private key that lives only on Sabrina's laptop at `%USERPROFILE%\.victron_cyd\ota_signing_key.pem`, never in the project or git. The public key is built into the firmware (`include/ota_pubkey.h`). The device installs nothing unless the manifest signature checks out and the downloaded file's size and SHA-256 match the manifest. Back up the private key: without it, no display can be updated over WiFi again (only by USB with a new key).
* **No downgrades:** only a version newer than the running one (`1.2.3` numbering) is offered.
* **HTTPS** with certificate checks against the roots in `include/ota_ca.h` (GitHub: USERTrust ECC / Sectigo E46; release file host: ISRG Root X1; plus sibling roots). `tools/make_ota_ca.py` rebuilds it.
* **When:** first check about a minute after boot once connected, then every 24 h (6 h after a failed check), and on demand from Settings > Data source > Check for updates. Checks need internet on the WiFi; in local mode without internet they just fail quietly and retry later.
* MQTT is closed during a check or install (two TLS sessions don't fit in RAM next to LVGL), then reconnects. A check takes a few seconds; the screens may briefly show NO CONNECTION.
* **Installs by itself (decision 2026-11-22, Sabrina: "I want new releases to auto-update so he doesn't have to do anything"; was: never without a tap):** when a check finds a newer signed version, it installs the next time the screen goes to sleep (after the logo, backlight off), so nobody has to do anything and nobody is interrupted. It stays dark while installing; a tap during it wakes the screen and shows the progress bar. One automatic try per version; after a failure it waits for the next check or a tap. Manual install stays: a blue `UPDATE` label top left on the data screens (tap, then "Update to X?" Later / Install) and Install X in the Data source menu install straight away, with a full-screen progress bar ("Keep it plugged in"). The board restarts when done. While installing, MQTT is closed, so an alarm in that minute shows after the restart.
* Testing a release before it goes out: flash it by USB to a separate test board first (no test channel, decided 2026-11-22). GitHub's "latest" link skips pre-releases, so a release must be published as a normal (latest) release for displays to see it.
* **Rollback:** the bootloader's app rollback is on. A new version must run 60 s before it's marked good; if it crashes or resets before that, the bootloader goes back to the previous version, and the menu shows "last update was undone".
* **Making a release** (on the laptop): bump `FW_VERSION`, commit, run `python tools/make_release.py` (builds both boards, signs, checks the signature against `ota_pubkey.h`, writes `dist/v<version>/`), then publish a GitHub release tagged `v<version>` with the four files. `tools/ota_keygen.py` made the key pair once.
* Not built yet: a browser flasher (ESP Web Tools on GitHub Pages) for the first flash of a new unit.

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

* Bottom bar, 40px tall, on every main screen: seven icon buttons, in order Overview, Battery, Solar, AC, Tanks, Alarms, Settings (Tanks added 2026-11-25). 3px between buttons as in the mockup, so each is about 40px wide; the active bar is 20px, and the alarm dot sits 5px from the top and 9px from the right.
* Active screen: card background behind the icon plus a blue bar on top
* Red dot on the Alarms icon when any alarm or warning is active
* No swipes. Taps only (resistive touch).
* Icons are the mockup's line icons, drawn into 20px alpha bitmaps by `tools/make_icons.py` (`include/nav_icons.h`) and colored at runtime (white active, muted otherwise). Tanks is a propane bottle with collar (24 viewBox, stroke 2, round caps and joins); at 20px its narrow collar notch fills in, the body and level line stay clear. A 14px copy is drawn for the Overview tank strip.
* Screens are built once and shown or hidden, so switching and waking are instant. Values refresh every 500 ms.
* LVGL has a 64 KB heap (56 KB until 1.0.4; it fits since the draw buffer moved to the heap in 1.0.0, and it gives room for the Tanks screen and strip). The boot log prints how much of it the screens use (`UI: built, LVGL memory N% used`).

### 6.3 Overview

* Left column: AC input tile on top, Solar tile below
* Center: SOC ring, 88px, filling to the SOC like a gauge, with SOC %, battery watts (`+` and green while charging) and voltage inside. Color by state: green charging, normal text discharging, amber at or below the low SOC setting (amber wins). Charging means battery power above 0.
* Right column: AC loads tile on top (watts, Quattro AC out amps below), Quattro state tile below
* **Quattro tile:** Charging (for Bulk, Absorption, Float, Storage, Equalize), Inverting, Passthru, Off, Fault (red), and so on. Below: the input limit while an AC input is live, otherwise the charge phase while charging, otherwise blank.
* **Solar tile:** total solar watts, and kWh today added up over all MPPTs
* **Tank strip** (2026-11-25, as in the Overview mockup): a 26px card along the bottom with the tank icon and one entry per LPG tank (up to two, sorted by name): the first word of its CustomName (Driver, Passenger) and its level as 5 bars. Lit bars = ceil(Level / 20), 0 when empty, unknown or on a sensor problem; lit bars are green, amber at or below 20%, unlit ones divider grey. No percent here. Tapping the strip opens Tanks; a tap that wakes the screen from snooze only wakes it (6.10). With the strip, tiles are 67px tall and the ring moves up to stay centered; with no LPG tank seen (yet), the strip is hidden and the tiles go back to 84px. Names are 10px (mockup 11px), so "Passenger" and its bars fit.
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

* **Since time:** the display has no clock (no internet in local mode, and no time sync), so it shows how long the alarm has been active as seen by the display: "Quattro • for 12m", "just now", "for 3h 05m", "for 2d 4h". The count starts when the display first sees it, so after a reboot it restarts.
* Messages: readable names for the common alarms (Overload, High temperature, AC input lost, Low state of charge, ...); any other name has its words split out (`HighStarterVoltage` becomes "High starter voltage"). MPPT error codes have names for the common ones ("PV input over voltage", "Charger temperature too high", ...), otherwise "Error N".
* Device: Quattro, SmartShunt, or the MPPT's CustomName.
* Our own low SOC warning is listed too ("SOC at or below 30%", device "Low SOC setting"), so the dot always has an entry to explain it.
* Order: alarms before warnings, newest first. Two cards fit; with more, a "1-2 of 5 • tap for more" line pages through them (no swipes).
* Up to 12 device alarms are tracked at once.

### 6.8 Settings

Five rows:

| Row | Options | Default |
|---|---|---|
| Low SOC warning | Steps of 5% | 30% |
| Snooze | 5s, 10s, 30s, 60s, 90s | 30s |
| Brightness | Steps of 10% | 70% |
| USB side | Left, Right: which side the USB cable comes out. Left turns the screen 180 degrees, at once. | Right |
| Data source | Shows Local or Cloud and status. Tap opens: Run setup, Recalibrate touch, Check for updates / Install | Auto |

* Layout as in the Settings mockup: one card per row, − / + buttons for Low SOC and Brightness, five buttons for Snooze with the chosen one in blue, Left / Right for USB side the same way. Rows are 6px apart with 26px buttons so five fit (the mockup has four)
* USB side: touch calibration is always saved for the normal (USB right) orientation and mirrored when flipped, so flipping needs no recalibration; calibrating while flipped saves it the normal way too. The flip applies from power-up, including calibration and setup screens
* Data source shows Local or Cloud (green when live, amber while connecting or failing, "No connection" in grey) with a short status below, such as "Live, 22/24 paths" or "VRM token rejected"
* The Data source menu is a card over a dimmed screen with Run setup (restarts into setup), Recalibrate touch, Check for updates ("Install X" when one is ready), a title with the display name ("Data source · Sabrina"), a line with the firmware version and update state, and Close (5.7)

All settings saved in NVS.

### 6.9 Setup (first boot, or Run setup)

* The CYD starts its own WiFi network, named per display so two nearby displays can't be mixed up: `Victron_CYD_<name>` (spaces become `_`), or `Victron_CYD_<4 hex digits from the MAC>` before it's named. The setup screen title shows "Setup: <name>". Its random password is made once per device on the first setup, kept in NVS, and reused every time setup opens, so a phone that joined before can rejoin on its own. The QR code and password text are still shown each time.
* Screen shows two QR codes: (1) join the setup WiFi (standard `WIFI:T:WPA;S:...;P:...;;` format), (2) open `http://192.168.4.1`. Print the network name, password and address as text too.
* Captive portal so most phones open the page on their own
* The page collects, in four sections: display name (optional, up to 20 letters, digits, spaces or dashes, such as "Sabrina" or "Dad"); trailer WiFi SSID and password; Cerbo address and port (prefilled `venus.local`:1883), optional GX username and password; VRM portal ID, VRM email, VRM token
* Saved secrets are never sent back to the page. Show "Saved" with a Replace option.
* On save: store in NVS, shut down the setup network, restart
* Opens on boot when no WiFi is saved, or after Run setup (sets an NVS flag and restarts). Run setup is in Settings → Data source.
* When rerunning setup, non-secret values (SSIDs, broker host/port, portal ID, VRM email) are prefilled. Passwords, GX password and token only show "Saved" / Replace; a blank field keeps the saved value.
* A Cancel button (shown only when settings already exist) restarts without saving
* The page checks: a WiFi is entered, port 1-65535, WiFi password empty (open network) or 8+ characters; warns if the VRM portal ID, email or token is missing (no cloud fallback). A pasted `Token ` prefix is stripped.

### 6.10 Snooze and logo

* After the snooze time with no touch: flash the logo for 1s, then backlight off
* WiFi and MQTT stay connected while snoozed
* Any tap wakes it and returns to the last screen. A tap during the logo cancels the snooze.
* The waking tap only wakes the screen; it never presses a button underneath
* No snooze on the setup screen (the QR codes need to stay visible) or in the Wokwi build (no touch to wake it)
* Logo: the original artwork in its own colors, cropped to the logo: 165x210 RGB565 (`include/logo_rgb565.h`, 69 KB, was 150 KB full screen), drawn on black at the same spot as in the 320x240 original. Made by `tools/make_logo.py` from `tools/logo_source_rgb565.h`. (The one-color pink version from earlier on 2026-11-22 is dropped at Sabrina's request; it also didn't show on the device, only a black screen. The boot log now says whether LVGL can read the logo and where it sits, and logs "Snooze: showing the logo" / "screen off".)

### 6.11 Alerts

* SOC at or below the low SOC setting: SOC turns amber and the Alarms dot shows
* Any device alarm: wake the screen and go to Alarms
  * Applies to device alarms and warnings. It happens once per alarm, when it appears or gets worse (warning to alarm), so tapping away doesn't get pulled back. Alarms already active at boot also show Alarms once the data arrives.
  * Low SOC doesn't wake or jump; it only turns amber and shows the dot.
* No sounds in v1

### 6.12 Tanks

Follows the Tanks mockup: one card per LPG tank, side by side.

* Shows tanks whose FluidType is 8 (LPG), up to two, sorted by CustomName (Driver before Passenger). A third LPG tank isn't shown; the log says how many there are. With one tank, only the left card shows. With none: "No LPG tanks found".
* Card: title is the CustomName in capitals with a trailing "Tank" dropped (DRIVER SIDE, PASSENGER SIDE). A level bar on the left (22px wide, filling from the bottom), then the big percent and three rows: Left (pounds, `19.2 lb`), Type (LPG), Status.
* Percent is 24px (the mockup's 30px isn't a built-in size, and 36px doesn't fit "100%" beside the bar). Green, amber at or below 20%; the bar follows. Status says OK (green) or Low (amber, at or below 20%).
* Pounds = Remaining (m³) × 264.172 × 4.24, one decimal. It relies on Capacity being set right on the Cerbo, so 20 lb and 40 lb bottles both work without code changes. No Remaining: `--`.
* Sensor problem (Status not Ok, or no Level): percent and pounds show `--` with a small amber "sensor" note under it, the bar is empty, and Status names the problem in amber with short words that fit the row: No signal (Disconnected), Shorted (Short circuited), Unknown, Config error. The log gives the full Venus name.
* No tank alarms (not asked for; Cerbo tank alarms don't work for every tank type).

## 7. Security and privacy rules

* Read only. Never publish to `W/` topics.
* No credentials, tokens, SSIDs or portal IDs in the source code or git. Everything comes from setup and lives in NVS.
* Never print secrets to serial logs. Mask them (for example `Token ****abcd`). Passwords log only as set/empty; the setup WiFi password is shown on screen, never logged.
* NVS is not encrypted in v1, so someone with the board in hand could read the saved values. Turning on ESP32 flash encryption is hard to undo, so it's left out; the read-only VRM user limits the damage.
* Cloud mode always verifies TLS certificates
* No telemetry. The only other server is GitHub, for update checks (5.7): it receives a plain download request, nothing about the system.
* Updates install only when signed with Sabrina's key, never to an older version, and roll back if they don't run (5.7). They install on their own while the screen sleeps.
* No notifications, emails or messages sent from the device

## 8. Later (don't build yet)

* Control functions (Quattro mode, input current limit), with confirm dialogs, off by default
* Temperature screens (tanks were built in 1.0.4, see 6.12)
* Battery power and charging
* Wake on pickup (motion sensor)
* History graphs (VRM API or local)
* Port to a larger display, sharing the same data layer
* OTA updates: built, see 5.7. Still later: a browser flasher (ESP Web Tools on GitHub Pages) for the first flash of a new unit. Each unit uses its own read only VRM user and token for its own installation.

## 9. Build checklist

- [x] Project scaffold, display and touch working, touch calibration saved (confirmed on the device with `cyd_ili9341`, including calibration kept after a power cycle)
- [x] Settings in NVS (confirmed on the device: setting changed on the demo screen kept after a power cycle)
- [x] Setup portal with QR codes (confirmed on the device: QR scan, captive page, save and restart, rerun shows Saved, Cancel)
- [x] Local MQTT: connect, portal ID discovery, keepalive (still to check at the trailer: join, connect, portal ID, paths seen, keepalive every 30s)
- [x] Topic paths verified against the live system (update 5.5) (confirmed over cloud; both MPPTs report)
- [x] Cloud MQTT: broker index, TLS, token login, error status (still to check on the device; can be tested at home by entering home WiFi as trailer WiFi)
- [x] Local first with cloud fallback, and switching back to local (changed to one WiFi, see 5.1 and 5.3; still to check at the trailer: local connects via `venus.local`, and if the Cerbo is briefly unreachable it goes to cloud and back to local within about 2 minutes)
- [x] Overview screen, including Shore/Generator last used (still to check on the device with live data)
- [x] Battery, Solar, AC, Alarms, Settings screens (still to check on the device)
- [x] Bottom nav and alarm dot (still to check on the device)
- [x] Stale data handling (whole-system freshness, see 5.6; still to check on the device)
- [x] Snooze with logo (still to check on the device)
- [x] Low SOC and alarm alerts (still to check on the device; alarm paths to verify in item 5)
- [ ] On device check: nothing cut off, all text readable
- [x] OTA updates from GitHub releases, signed, with tap to install and rollback (5.7) (confirmed on the device: 1.0.1 by tap, 1.0.3 automatically)
- [x] Tanks: Mopeka LPG data, Tanks screen, 7-icon nav, Overview tank strip (5.5, 6.2, 6.3, 6.12) (still to check on the device: the six tank paths in the "seen" log, names and pounds against the bottles, sensor-problem display, strip tap, nothing cut off, LVGL memory)

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
| 2026-11-22 | Item 13 built, finishing items 9 and 10: Quattro and battery alarms (0/1/2, Quattro L1 copies merged) and MPPT error codes (non-zero = alarm) are kept in the data layer with readable names; Alarms screen from the mockup; the dot shows any alarm, warning or low SOC; a new or worse device alarm wakes the screen and shows Alarms once. "Since" is shown as a duration, since the display has no clock. Alarms now greys out when data is stale. Corrected 5.5: ErrorCode is an error number, not 0/1/2. |
| 2026-11-22 | Pre-flash review fixes: taps on the active-page bar or the alarm dot in the nav, and on the Data source text or chevron in Settings, were caught by non-interactive containers and did nothing; those containers no longer take taps. Labels that end in "..." no longer redraw every 500 ms. The Alarms "tap for more" line now changes page at once. The net task logs its spare stack after subscribing, to confirm on the device that 12 KB is enough for TLS. |
| 2026-11-22 | Item 5 mostly done from the first cloud boot log: every 5.5 path confirmed (notes in 5.5). Solar power below zero now shows 0. LVGL memory 72% used, net task 8.9 KB stack spare. `logs/` added to .gitignore. Passenger MPPT still to confirm. |
| 2026-11-22 | Item 5 done: Passenger MPPT confirmed. New USB side setting (6.8): Left turns the screen 180 degrees, saved as `flip`; touch is mirrored so no recalibration is needed. Settings rows tightened to fit five. |
| 2026-11-22 | One WiFi instead of two (Sabrina: the Cerbo is on the trailer WiFi, no need for both). Setup has one WiFi, a Cerbo address (default `venus.local`, looked up over mDNS) and VRM. Each attempt joins the WiFi, tries the Cerbo directly, then VRM over the same WiFi. While on cloud, a connection check to the Cerbo every 2 minutes (backing off as before) replaces the hotspot scan, and switching keeps the WiFi up. Old hotspot settings carry over. New status texts: "Cerbo not found on WiFi", "Cerbo broker" / "VRM broker not reachable". Sections 1, 3, 5.1-5.3, 6.9 updated. |
| 2026-11-22 | Fixes from the first device run: the board crashed in a loop when the Cerbo address didn't resolve (MQTT client cleanup before any connect); Settings controls were drawn off to the right and low, clipped to their last button (seen in a photo), now laid out as a flex row. |
| 2026-11-22 | OTA updates built (5.7), moved up from Later at Sabrina's request: two 1.94 MB slots (`partitions_ota.csv`, one USB flash to switch), daily signed-manifest check on GitHub releases, UPDATE label and Install dialog, progress screen, bootloader rollback after 60 s. Signing key made with `tools/ota_keygen.py` (private key outside the repo), releases with `tools/make_release.py`. Logo now one color (solar pink) as a cropped alpha mask, 150 KB to 34 KB. LVGL draw buffer moved from static RAM to the heap to make room. Firmware 1.0.0. |
| 2026-11-22 | Display names, so Sabrina's and Dad's displays parked near each other can't be confused: optional name in setup (`net/name`), setup network `Victron_CYD_<name>` (or a MAC-based suffix before naming), name in the setup title, the Data source menu, the log and the mDNS host name (`cyd-<name>`). Local mode now refuses a Cerbo whose portal ID differs from setup and falls back to cloud (5.4). OTA repo set: `SheMunkee/trailer-display-cyd`. |
| 2026-11-22 | Firmware 1.0.1: first OTA test release (no other changes). |
| 2026-11-22 | Updates install automatically (5.7), Sabrina's call so Dad never has to do anything: checked a minute after boot and daily, installed the next time the screen sleeps, quietly in the dark. Manual Check / Install stays. Signing, no-downgrade and rollback unchanged. Test releases go to a separate test board by USB, no test channel. |
| 2026-11-22 | 1.0.1 installed over WiFi on Sabrina's board (first OTA confirmed on the device). Firmware 1.0.2: automatic updates. |
| 2026-11-22 | Firmware 1.0.3: snooze logo back to the original colors, cropped (69 KB; about 350 KB free in each firmware slot). The one-color logo showed only a black screen on the device; snooze now logs what it does to help find out why if it recurs. |
| 2026-11-22 | Added README.md (build, flash, release, Wokwi) and docs/SETUP.md (end user setup guide for a new display). |
| 2026-11-25 | docs/SETUP.md aligned with the firmware: exact screen and status texts, GX password optional (the display falls back to VRM when it can't read the Cerbo directly, no profile is required), troubleshooting by status line, STALE / NO DATA / NO CONNECTION markers, update check timing. README release steps now say to push before creating the release and to test on a separate test board. No firmware change. |
| 2026-11-25 | Tanks built (moved from Later; temperature stays in Later), firmware 1.0.4. Data: `tank/+/Level, Remaining, Capacity, CustomName, FluidType, Status` (checked against the Venus dbus wiki: Level %, Remaining and Capacity m³, LPG = 8, Status 0 = Ok), new tanks group with its own update time. Tanks screen from the mockup (6.12), nav now 7 icons with Tanks between AC and Alarms (6.2), Overview tank strip with 5-bar levels that opens Tanks (6.3). Decisions (Sabrina): titles drop a trailing "Tank"; sensor problems show `--`, a "sensor" note and short status words; up to two tanks sorted by name; strip hidden when there are no LPG tanks. LVGL heap 56 to 64 KB. OTA repo spelled `shemunkee/trailer-display-cyd` (GitHub's own spelling). docs/SETUP.md updated (31 paths, Tanks screen, Overview strip). |
| 2026-11-26 | Screen mockups moved into the repo as `design/screens.html` with sources in `design/src`, so other projects can reference them. Setup mockup now shows `Victron_CYD_<name>` and "Setup: <name>". No firmware change. |
