# Trailer Display (CYD)

A small touch screen that shows a Victron RV power system live: battery, solar, shore or generator, AC loads, the Quattro and alarms. It runs on a $12 "Cheap Yellow Display" (ESP32-2432S028R) and reads the Cerbo GX over MQTT, on the trailer WiFi first and through the Victron VRM cloud when the Cerbo isn't reachable.

It's read only. It never changes anything on the Victron system.

* **Setting up a display you were given?** Follow [docs/SETUP.md](docs/SETUP.md).
* **Building, flashing or releasing?** Keep reading.
* **What it does, in detail:** [SPEC.md](SPEC.md) is the source of truth.

---

## What you need

| Item | Notes |
|---|---|
| CYD board, ESP32-2432S028R | Single USB-C version. The screen chip is ILI9341 or ST7789 depending on the batch (see Flashing). |
| USB-A to USB-C cable | This board does **not** power up from a USB-C to USB-C cable. Use USB-A at the power end. For the wall case, a right angle USB-C end. |
| USB-A power adapter | Any 5V phone charger. |
| A Windows, Mac or Linux computer | For the first flash only. Updates after that arrive over WiFi. |
| Victron Cerbo GX | On the trailer WiFi, with MQTT switched on (see docs/SETUP.md). |

## One time computer setup

1. Install [Git](https://git-scm.com/downloads) and [Python 3](https://www.python.org/downloads/).
2. Install PlatformIO:
   ```
   pip install platformio
   ```
3. Get the project:
   ```
   cd C:\Users\<you>\code
   git clone https://github.com/shemunkee/trailer-display-cyd
   cd trailer-display-cyd
   ```
4. If Windows doesn't show a COM port when the board is plugged in, install the CH340 USB driver.

## Flashing a new board

1. Plug the CYD into the computer with a USB-A to USB-C cable (a USB-C to USB-A adapter or hub works).
2. Find its COM port:
   ```
   pio device list
   ```
   It shows up as `USB-SERIAL CH340`, for example `COM7`.
3. Build and flash. The first build takes 5 to 10 minutes while PlatformIO downloads its tools.
   ```
   pio run -e cyd_ili9341 -t upload --upload-port COM7
   ```
   Watch for `Writing at 0x... (100 %)`, `Hash of data verified` and `SUCCESS`.
4. **Screen stays white, black or shows static?** The board has the other screen chip. Flash the other build:
   ```
   pio run -e cyd_st7789 -t upload --upload-port COM7
   ```
5. **Stuck on `Connecting....`?** Hold **BOOT** on the back of the board, tap **RST**, release BOOT. It should start writing within a few seconds.

After flashing, the board asks for touch calibration (tap the center of each target with a fingernail), then opens the setup screen. From here the person who'll use it follows [docs/SETUP.md](docs/SETUP.md).

### Preparing a display for someone else

1. Flash it as above and do the touch calibration.
2. Leave it on the setup screen and unplug it. Calibration is saved, and setup opens again on the next power up.
3. Send it with a USB-A to USB-C cable, a USB-A charger and a link to [docs/SETUP.md](docs/SETUP.md).
4. Help them make a **read only VRM user and token for their own installation** (never share yours).

Every display checks GitHub for updates once a day, so once it's online it stays current without you touching it again.

## Updates over WiFi (OTA)

Displays install new releases on their own the next time the screen goes to sleep. See [SPEC.md 5.7](SPEC.md) for the full design.

**Rules that keep it safe**

* This repo must stay **public**. Displays download releases without logging in.
* Releases are signed. The private key lives only at `%USERPROFILE%\.victron_cyd\ota_signing_key.pem` and is never committed. **Back it up somewhere safe.** Without it, no display can be updated over WiFi again.
* Displays only install a version newer than the one they run, and only if the signature and file checksums match.
* If a new version crashes in its first 60 seconds, the board rolls back to the previous one by itself.

**Making a release**

1. Test the change on your own board by USB first.
2. Bump `FW_VERSION` in `include/version.h` (for example `1.0.3` to `1.0.4`) and commit.
3. Build and sign both board types:
   ```
   python tools/make_release.py
   ```
   This writes four files to `dist/v<version>/`.
4. On GitHub, create a release tagged `v<version>` (for example `v1.0.4`) and attach all four files:
   `firmware-cyd_ili9341.bin`, `firmware-cyd_st7789.bin`, `manifest.json`, `manifest.sig`.
5. Publish it as a normal release, not a pre release. Displays only see the latest release.

Displays pick it up within a day, or straight away from Settings > Data source > Check for updates.

## Demo without a board (Wokwi)

The `wokwi` build runs in the [Wokwi](https://wokwi.com) simulator in VS Code. It has no touch controller, so touch and calibration are skipped.

```
pio run -e wokwi
```

Then start the simulator from the Wokwi extension. It reads `wokwi.toml` and `diagram.json`.

## Project layout

| Path | What it is |
|---|---|
| `src/` | Firmware: screens, MQTT link, setup portal, OTA |
| `include/` | Shared headers: version, certificates, OTA public key, logo, icons |
| `tools/` | Helper scripts: release signing, key generation, logo and icon builders |
| `platformio.ini` | Build settings and the three builds (`cyd_ili9341`, `cyd_st7789`, `wokwi`) |
| `partitions_ota.csv` | Flash layout with two firmware slots for OTA |
| `SPEC.md` | What the device does, decisions and change log |
| `CLAUDE.md` | Rules for Claude Code in this project |

## Security

* Read only. The firmware never publishes to Victron `W/` topics.
* No passwords, tokens or network names in the code. They're entered on the device's setup page and stored only on the device.
* Cloud mode always checks TLS certificates.
* The board's storage isn't encrypted. If a display is lost, delete its VRM token in VRM and change the GX password.
