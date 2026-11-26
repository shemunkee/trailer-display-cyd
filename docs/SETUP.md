# Setting up your Trailer Display

This little screen shows your Victron system live: battery, solar, shore or generator power, what's running, and any alarms. Setup takes about 15 minutes, and you only do it once.

You'll need your phone, your trailer WiFi password, and access to your Cerbo's settings (on its screen, or Remote Console on VRM).

---

## Part 1: Get your Victron details ready

Do this first, at the trailer or from VRM. Write each one down.

### On the Cerbo

1. **Join the trailer WiFi.** Settings > WiFi. The Cerbo and the display both use the trailer WiFi.
2. **Turn on MQTT.** This is how the display reads the data straight from the Cerbo. On newer Cerbo software it's under Settings > Integrations; on older versions, Settings > Services. Switch MQTT on.
3. **Check the GX password.** Settings > General > Access & security shows the Cerbo's security profile. If it uses a password, note it down: the display needs it to read the Cerbo directly. If there's no password, leave that field empty in Part 2.
4. **Note the VRM Portal ID.** Settings > VRM online portal > VRM Portal ID. It's 12 letters and numbers, like `c0a1b2d3e4f5`.

If the display can't read the Cerbo directly (MQTT off, wrong password, or a security setting that doesn't allow it), it still works: it reads the same data through VRM over the internet instead.

### On VRM (vrm.victronenergy.com)

This lets the display work through the internet when it can't reach the Cerbo directly.

1. **Make a read only user.** In your installation's settings, invite a second email address you control and give it read only access. Then log in as that user and accept the invite.
2. **Make a token.** While logged in as the read only user, go to Preferences > Integrations > Access tokens and create one. **Copy it right away.** VRM only shows it once.

Why a separate user: if the display is ever lost, the worst anyone could do is see your battery level. You can delete the token anytime.

**Write down:** trailer WiFi name and password, GX password (if there is one), VRM Portal ID, read only user's email, token.

---

## Part 2: Set up the display

1. **Plug it in** with the cable that came with it, into a **USB-A** charger (the rectangular kind). A USB-C charger won't power this board.
2. **If it shows crosses to tap** ("Tap the center of the cross", 4 points), tap the center of each one with a fingernail. This teaches it where you touch. If it says "That didn't line up", just do it again.
3. **The setup screen shows two QR codes**, titled "Setup".
4. **Scan QR code 1** ("Join setup WiFi") with your phone camera. Your phone joins the display's own WiFi, named `Victron_CYD_` plus 4 letters and numbers (after you name the display, `Victron_CYD_` plus its name). The network name and password are also printed on screen if the scan doesn't work.
5. **The setup page opens on its own.** If it doesn't, scan QR code 2 ("Open the page") or go to `http://192.168.4.1` in your phone's browser.
6. **Fill in the page:**

   | Section | Field | What to enter |
   |---|---|---|
   | This display | Display name | Optional, to tell it apart from other displays nearby, for example `Dad`. Up to 20 letters, numbers, spaces or dashes. |
   | Trailer WiFi | WiFi network name (SSID) | Your trailer WiFi name, exactly as it appears |
   | | WiFi password | Your trailer WiFi password (8 characters or more; leave empty only for an open network) |
   | Cerbo (local mode) | Cerbo address | Leave it as `venus.local` |
   | | Port | Leave it as `1883` |
   | | GX username (optional) | Leave it blank |
   | | GX password (optional) | The GX password from Part 1, or empty if your Cerbo has none |
   | VRM (cloud mode) | VRM portal ID | The 12 character Portal ID |
   | | VRM email | The **read only** user's email |
   | | VRM access token | The token. Paste just the token. |

7. **Tap Save and restart.** The page says "Saved", and the display shows "Saved, restarting" and connects. Your phone goes back to its normal WiFi.

If the page warns that the VRM portal ID, email or token is missing, it saved anyway, but there's no internet fallback until you add them.

Passwords and the token are never shown again on the setup page. It just says **Saved**, with a Replace button if you need to change one. Leaving a password field empty keeps the saved one.

---

## Part 3: Check it's working

On the display, tap the **Settings** icon (far right of the bottom bar) and look at the **Data source** row. The top word says how it's connected, and the small line under it says what it's doing:

| Top word | Meaning |
|---|---|
| **Local**, in green | Connected straight to the Cerbo over the trailer WiFi. Best case. |
| **Cloud**, in green | Connected through VRM over the internet. Works just as well. |
| **Local** or **Cloud** in amber | Still connecting, or that connection failed. Read the small line (see Troubleshooting). |
| **No connection**, in grey | Not connected to anything yet. Read the small line. |

When it's working, the small line says **Live, 31/31 paths** (or a number close to 31; a system without propane sensors shows about 25). Just after a restart it says **Loading, ...** for a few seconds.

If it says **Cloud** while you're at the trailer, the display couldn't read the Cerbo directly and is using the internet instead. That's fine to leave, but Troubleshooting has tips for getting it to Local.

---

## Using it

Tap the icons along the bottom to switch screens:

| Icon | Screen | Shows |
|---|---|---|
| House | Overview | Shore or generator power, solar (and kWh today), battery % with power and volts, AC loads, Quattro state; along the bottom, each propane tank as 5 bars (tap it for Tanks) |
| Battery | Battery | Charge %, Voltage, Current, Power, Time to go, Consumed |
| Sun | Solar | Total solar, and for each charger: power, PV voltage, state and today's kWh |
| Plug | AC | AC input (Shore or Generator): voltage, power, current limit; AC output: voltage, current, frequency; Quattro state |
| Propane bottle | Tanks | Each propane tank (Mopeka sensor): level %, pounds left, status. Amber at 20% or less. "sensor" with a status like "No signal" means the reading isn't coming through. |
| Bell | Alarms | Active alarms and warnings, two at a time ("tap for more" shows the rest). A red dot on the bell means something needs a look. |
| Sliders | Settings | Low SOC warning, Snooze time, Brightness, USB side, Data source |

* **Snooze:** after the snooze time with no touch (30 seconds to start; 5, 10, 30, 60 or 90 in Settings), it shows the logo for a second and the screen goes dark. Tap anywhere to wake it; that tap only wakes it. It keeps watching your system while asleep.
* **Low battery:** at or below your Low SOC level (30% to start), the battery number turns amber and the bell gets a red dot.
* **Alarms** from the Victron devices wake the screen and jump to the Alarms page. The display can't change anything, so clear alarms on the Cerbo.
* **Greyed out numbers** with an amber word top right mean the numbers on screen may be out of date:
  * **NO CONNECTION**: the display isn't connected right now; it keeps retrying.
  * **STALE**: connected, but nothing has arrived for 30 seconds.
  * **NO DATA**: connected, but nothing has ever arrived (usually a wrong VRM Portal ID).
* **Cable on the other side?** Settings > USB side > Left turns the screen around. Touch follows, no recalibration needed.

### Updates

The display updates itself. It checks about a minute after it starts and then once a day. When a new version comes out, it installs the next time the screen goes to sleep, and you won't notice. **Just keep it plugged in.** If you happen to tap it mid update, you'll see a progress bar ("Keep it plugged in"), and it restarts when done.

To check by hand: Settings > Data source > Check for updates. If one is found, the button changes to **Install** with the version number; a blue **UPDATE** label also shows top left on the other screens. Tap either to install straight away.

---

## Troubleshooting

Most problems show up as the small line under **Data source** in Settings.

| You see | Fix |
|---|---|
| Screen stays dark when plugged in | Use a **USB-A** charger with the included cable. USB-C to USB-C won't power it. |
| Taps land in the wrong place | Settings > Data source > Recalibrate touch. Or hold your finger on the screen while plugging it in until it says "Keep holding to recalibrate touch". |
| "Saved WiFi not in range" or "Couldn't join WiFi" | Check the trailer WiFi name and password: Settings > Data source > Run setup. |
| "Cerbo not found on WiFi" | The display can't find `venus.local`. Run setup and enter your Cerbo's IP address (shown in the Cerbo's WiFi settings) instead. |
| "Cerbo broker not reachable" | Check MQTT is switched on in the Cerbo (Part 1). |
| "GX login rejected" | Run setup and enter (or Replace) the GX password. |
| "Different Cerbo found (portal ID)" | Another Cerbo on the same WiFi answered. Run setup and enter your Cerbo's IP address instead of `venus.local`. |
| "VRM token rejected" | The token must belong to the email you entered. Log in to VRM as the read only user, make a new token, then Run setup and Replace it. |
| "VRM broker not reachable" | The trailer WiFi has no internet right now. It keeps retrying. |
| "Portal ID missing in setup" | Run setup and enter the VRM Portal ID. |
| **Cloud** at the trailer instead of Local | It couldn't read the Cerbo directly. Check MQTT is on and the GX password, or try the Cerbo's IP address instead of `venus.local`. It works fine on Cloud meanwhile. |
| **NO DATA** top right | Run setup and check the VRM Portal ID. |
| Shows the wrong source (Shore vs Generator) | On the Cerbo, check Settings > System setup > AC input 1 and 2 are set to Shore and Generator. |
| Can't open the setup page | Turn off mobile data on your phone, rejoin the `Victron_CYD_...` WiFi, then go to `http://192.168.4.1`. |
| Need to change any setting | Settings > Data source > Run setup. Saved values stay unless you replace them, and Cancel leaves without saving. |

Still stuck? Send Sabrina a photo of the Settings screen.
