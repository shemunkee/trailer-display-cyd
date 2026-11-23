# Setting up your Trailer Display

This little screen shows your Victron system live: battery, solar, shore or generator power, what's running, and any alarms. Setup takes about 15 minutes, and you only do it once.

You'll need your phone, your trailer WiFi password, and access to your Cerbo's settings (on its screen, or Remote Console on VRM).

---

## Part 1: Get your Victron details ready

Do this first, at the trailer or from VRM. Write each one down.

### On the Cerbo

1. **Join the trailer WiFi.** Settings > WiFi. The Cerbo and the display both use the trailer WiFi.
2. **Turn on MQTT.** Settings > Integrations > MQTT, switch it on. This is how the display reads the data.
3. **Set the security profile to Weak.** Settings > General > Access & security.
   * **Weak** asks for a password. That's the one to use.
   * **Secured** blocks the display, so the screens stay blank.
   * Never pick **Unsecured**. It lets anyone on the WiFi change your settings.
4. **Note the GX password** you set there.
5. **Note the VRM Portal ID.** Settings > VRM online portal > VRM Portal ID. It's 12 letters and numbers, like `c0a1b2d3e4f5`.

### On VRM (vrm.victronenergy.com)

This lets the display work through the internet when it can't reach the Cerbo directly.

1. **Make a read only user.** In your installation's settings, invite a second email address you control and give it read only access. Then log in as that user and accept the invite.
2. **Make a token.** While logged in as the read only user, go to Preferences > Integrations > Access tokens and create one. **Copy it right away.** VRM only shows it once.

Why a separate user: if the display is ever lost, the worst anyone could do is see your battery level. You can delete the token anytime.

**Write down:** trailer WiFi name and password, GX password, VRM Portal ID, read only user's email, token.

---

## Part 2: Set up the display

1. **Plug it in** with the cable that came with it, into a **USB-A** charger (the rectangular kind). A USB-C charger won't power this board.
2. **If it shows tap targets,** tap the center of each one with a fingernail. This teaches it where you touch.
3. **The setup screen shows two QR codes.**
4. **Scan QR code 1** with your phone camera. Your phone joins the display's own WiFi, named `Victron_CYD_...`. The name and password are also printed on screen if the scan doesn't work.
5. **The setup page opens on its own.** If it doesn't, scan QR code 2 or go to `http://192.168.4.1` in your phone's browser.
6. **Fill in the page:**

   | Section | Field | What to enter |
   |---|---|---|
   | This display | Display name | Anything you like, for example `Dad` |
   | Trailer WiFi | WiFi network name | Your trailer WiFi name, exactly as it appears |
   | | WiFi password | Your trailer WiFi password |
   | Cerbo (local mode) | Cerbo address | Leave it as `venus.local` |
   | | Port | Leave it as `1883` |
   | | GX username | Leave it blank |
   | | GX password | The GX password from Part 1 |
   | VRM (cloud mode) | VRM portal ID | The 12 character Portal ID |
   | | VRM email | The **read only** user's email |
   | | VRM access token | The token. Paste just the token. |

7. **Tap Save and restart.** The display restarts and connects. Your phone goes back to its normal WiFi.

Passwords and the token are never shown again on the setup page. It just says **Saved**, with a Replace button if you need to change one.

---

## Part 3: Check it's working

On the display, tap the **Settings** icon (far right of the bottom bar) and look at the **Data source** row:

| It says | Meaning |
|---|---|
| **Local**, in green | Connected straight to the Cerbo over the trailer WiFi. Best case. |
| **Cloud**, in green | Connected through VRM over the internet. Fine too. |
| Amber, "connecting" | Give it a minute after a restart. |
| "VRM token rejected" | The email and token don't match. See Troubleshooting. |
| "No connection" | It can't reach the WiFi or the Cerbo. See Troubleshooting. |

---

## Using it

Tap the icons along the bottom to switch screens:

| Icon | Screen | Shows |
|---|---|---|
| House | Overview | Shore or generator, solar, battery %, what's running, inverter state |
| Battery | Battery | Charge %, volts, amps, watts, time left |
| Sun | Solar | Each solar charger's power and today's total |
| Plug | AC | Shore or generator input, power going out, inverter state |
| Bell | Alarms | Active alarms and warnings. A red dot on the bell means something needs a look. |
| Sliders | Settings | Low battery warning, sleep time, brightness, which side the cable comes out |

* **Sleep:** after a short time with no touch, it shows the logo and the screen goes dark. Tap anywhere to wake it. It keeps watching your system while asleep.
* **Low battery:** below your warning level (30% to start), the battery number turns amber and the bell gets a red dot.
* **Alarms** wake the screen and jump to the Alarms page. The display can't change anything, so clear alarms on the Cerbo.
* **Greyed out numbers** mean the data hasn't updated in the last 30 seconds.
* **Cable on the other side?** Settings > USB side flips the screen around.

### Updates

The display updates itself. When a new version comes out, it installs the next time the screen goes to sleep, and you won't notice. **Just keep it plugged in.** If you happen to tap it mid update, you'll see a progress bar, and it restarts when done.

To check by hand: Settings > Data source > Check for updates.

---

## Troubleshooting

| Problem | Fix |
|---|---|
| Screen stays dark when plugged in | Use a **USB-A** charger with the included cable. USB-C to USB-C won't power it. |
| Taps land in the wrong place | Settings > Data source > Recalibrate touch. Or hold your finger on the screen while plugging it in. |
| Numbers are blank but it says connected | On the Cerbo, check MQTT is on and the security profile is **Weak**, not Secured. |
| "VRM token rejected" | The token must belong to the email you entered. Log in to VRM as the read only user, make a new token, then Settings > Data source > Run setup and replace it. |
| "No connection" | Check the trailer WiFi name and password. Settings > Data source > Run setup lets you fix them. |
| "Different Cerbo found" | Another Cerbo on the same WiFi answered. Run setup and enter your Cerbo's IP address instead of `venus.local`. |
| Shows the wrong source (Shore vs Generator) | On the Cerbo, check Settings > System setup > AC input 1 and 2 are set to Shore and Generator. |
| Can't open the setup page | Turn off mobile data on your phone, rejoin the `Victron_CYD_...` WiFi, then go to `http://192.168.4.1`. |
| Need to change any setting | Settings > Data source > Run setup. Saved values stay unless you replace them. |

Still stuck? Send Sabrina a photo of the Settings screen.
