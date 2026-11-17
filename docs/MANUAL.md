# Sleazy Crow — Instruction Manual

How to use every function, step by step. Written for the phone app (Android). The Crow
does the radio work; the app is the remote control and the screen.

A quick note up front: this is a radio test tool. Use it on your own gear, or on gear
you have permission to test. Transmitting and jamming are regulated in most places — know
your local rules before you key up.

---

## 1. Getting started

**Flash the firmware.** Open the web flasher at
**https://dagromflomite.github.io/SleazyCrow/** in desktop Chrome, Edge or Opera, plug the
Crow in by USB, and follow the prompts. (Or use esptool with the files from the Releases
page.)

**Install the app.** Grab `SleazyCrow-vX.Y.Z.apk` from the Releases page and install it. It
sits next to the official Evil Crow app; both talk to the Crow the same way.

**Connect.**
1. Open the app. The Home screen lists nearby Crows.
2. Tap **Connect** next to yours.
3. The top bar fills in — two radio icons, the nRF icon, battery, free memory and
   temperature — and the status reads **Connected**.

If the first connect times out, turn the phone's Bluetooth off and back on, then try again.
That clears a stale pairing cache on Android. It's a phone quirk, not the Crow.

Only one thing can hold the Crow's Bluetooth at a time. If you want the PC connected, turn
the phone's Bluetooth off first, and vice versa.

---

## 2. The top status bar

It's on every screen once you're connected:

- **Two antenna icons (1, 2)** — the two sub-GHz radios. Grey means idle, coloured means in
  use.
- **nRF icon** — the 2.4 GHz radio. Turns orange when it's transmitting (jammer) or busy.
- **Battery** — charge level, with a bolt when it's on USB power.
- **Chip number** — free memory, in KB. Lower is fine; it drops while a job runs.
- **Thermometer** — the Crow's temperature.

---

## 3. Home → RF scanner

See which frequencies around you are busy.

1. On the Home tab, pick a radio: **M1**, **M2**, or **1+2** for both.
2. Choose **Spectrum** (a bar per frequency) or **Signal List** (decoded hits).
3. Tap the **play** button (top right). It reads **Scanning active**.
4. Bars rise where there's activity. Colour shows strength: red strong, orange medium,
   green weak.
5. Tap **stop** (the red square) when you're done.

---

## 4. Home → TPMS ("is a car following me?")

Every car's tires broadcast a sensor ID, pressure and temperature. This listens for them
and flags any that keep showing up near you over time — a sign the same car is around.

1. Tap the **TPMS** tab at the top.
2. Pick the **band**: **315 MHz** (most US cars) or **433.92 MHz** (most EU cars). If you're
   not sure, try one, then the other.
3. Set **Flag a follower when** to how quick you want the warning (Immediate is the
   touchiest).
4. Tap **Scan**. It starts listening.
5. Each car that comes through shows up as a card: the sensor ID, the make, the pressure and
   temperature. A card turns red and says **FOLLOWER** when it matches your follower rule.
6. **Clear** empties the list. The three-dot menu has more options, including saving known
   followers so your own car doesn't keep tripping it.

Tip before a drive: clear the list so sensors from where you parked don't look like
followers.

---

## 5. Sub-GHz → Record (capture a signal)

1. Tap **Sub-GHz** at the bottom, then the **Record** tab.
2. Pick **Module 1** or **Module 2**, and the **Recording** toggle.
3. Set the **frequency** (for example 433.92) and the **preset** (AM 270 for most simple
   remotes). **Advanced** has finer radio settings if you need them.
4. Tap **Start Recording**. It reads **Recording** with the live radio settings.
5. Trigger the thing you want to capture (press the remote). The signal appears under
   **Signals Captured**.
6. Tap **Stop Recording**.
7. Each capture has a **save** icon (keep it on the SD card) and a **send** icon (transmit
   it — see next).

---

## 6. Sub-GHz → Replay / transmit

1. On the Record tab, find your saved signal under **Signals Captured** (or load one with
   the folder icon, or use **Play Sub File**).
2. Tap the **send** (arrow) icon.
3. A safety box pops up — **Tap to Transmit** to send it, Cancel to back out.
4. The Crow transmits the exact signal it recorded.

You can also send a raw on/off pattern on a chosen frequency from the transmit controls.

---

## 7. Sub-GHz → Brute force

Walk through every possible code for a garage or gate protocol.

1. Tap the **Brute** tab.
2. Filter by category (All, EU Garage, US Garage, Home Auto, Alarm) or search by name.
3. Pick **Standard** or **DeBruijn** (DeBruijn is a faster sweep where the protocol allows
   it).
4. Tap the **play** arrow on a protocol (CAME, Princeton, NICE, Holtek, FAAC, BFT, …).
5. Check the details (frequency, timing, estimated time) and tap **Start**.
6. A bar shows progress (codes sent / total). **Pause** and **Stop** are there the whole
   time.

Some protocols have a huge code space and take hours — the estimate tells you which.

---

## 8. Sub-GHz → ProtoPirate (car key fobs)

Read a car key fob, and for the supported makes build and send the next rolling code. See
[PROTOPIRATE-SUPPORTED.md](PROTOPIRATE-SUPPORTED.md) for which makes do what.

1. Tap the **ProtoPirate** tab.
2. Pick the **frequency** (433.92, 315, 868.35 or 303.87 MHz — most fobs are 433.92 or 315)
   and the **module**.
3. Tap **Start Decode**.
4. Press the fob. If it's a make the Crow knows, a card shows the make, button and rolling
   counter. If it isn't, it shows **Unknown** with the raw data, so you still know it heard
   something.
5. **Next code (supported makes only):** load the maker key first — in **Settings** or from a
   text file on the SD card — then use the next-code / transmit option on the decoded card.
   No keys ship with the firmware; reading a fob does not reveal its key.

For testing on fobs and vehicles you own or are allowed to test.

---

## 9. Sub-GHz → Playlists

Send a set of saved `.sub` files in order.

1. Tap the **Playlist** tab.
2. Tap **New playlist**, give it a name, tap **OK**.
3. Add saved `.sub` files in the order you want, set any pauses.
4. Play the list to send them back to back with one tap.

---

## 10. Sub-GHz → Jamming

On the Record tab, the **Jamming** toggle (instead of Recording) floods a sub-GHz
frequency. Only on gear you own, where it's legal. Start it, and stop it the same way.

---

## 11. NRF → Spectrum (2.4 GHz)

1. Tap **NRF** at the bottom, then the **Spectrum** tab.
2. Tap **Start Analyzer**.
3. Bars show busy 2.4 GHz channels (Wi-Fi, Bluetooth, and so on) from 2.400 to 2.525 GHz.
4. **Stop** when done.

---

## 12. NRF → Jammer (2.4 GHz)

Flood a 2.4 GHz band. For testing on your own gear, where it's legal.

1. Tap the **Jammer** tab.
2. Pick a **mode**: Full Spectrum, Wi-Fi, BLE, Bluetooth, USB Wireless, Video, RC, Single
   Channel, Custom Hopper, Zigbee or Drone. The gear icon on each sets its options.
3. The **Dwell Time** slider sets how long it sits on each channel — all the way left is
   Turbo (fastest hopping).
4. Tap **Start Jammer**. The nRF icon up top goes orange.
5. Tap **Stop Jammer** to end it.

---

## 13. NRF → MouseJack

Scan for wireless mice and keyboards that use vulnerable 2.4 GHz dongles.

1. Tap the **MouseJack** tab.
2. Tap **Start Scan**. It sweeps the 2.4 GHz channels looking for those devices.
3. Anything it finds lists under **Targets**. **Hide Unknown** filters out ones it can't
   identify.
4. **Stop Scan** to end it.

(To see a target you need a real vulnerable dongle in range; there's nothing to find
otherwise.)

---

## 14. Files

Browse and manage files on the Crow over Bluetooth.

1. Tap **Files** at the bottom.
2. Switch between **SD Card** and **Internal** storage at the top.
3. Tap a folder to open it; the breadcrumb (Root / …) walks you back.
4. The toolbar: **refresh**, **new folder**, **upload**, and **multi-select** (for
   renaming or deleting several at once).

---

## 15. Settings

1. Tap **Settings** at the bottom.
2. **SDR Mode** — turn the Crow into a plain spectrum/raw receiver over USB.
3. **App Settings** — language, cache, permissions.
4. **RF Settings** — defaults for brute force, radio and scanner.
5. **HW Buttons** — what the Crow's physical buttons do.
6. **nRF24 Settings** — 2.4 GHz power level, data rate and channel.
7. **Device Management** — rename the Crow, and other device options.

---

## 16. Firmware update

1. In **Settings**, open **Firmware Update**. It shows the version on the Crow.
2. Tap **Check FW Version**, then **OTA Update**.
3. The app checks GitHub. If there's a newer release it offers to flash it over Bluetooth;
   if not, it says **No new version available**.
4. Don't unplug or walk away mid-flash.

The app will also tell you on its own when a newer firmware or app version is out.

---

## 17. If something goes wrong

- **Won't connect / times out** — turn the phone's Bluetooth off and on, then reconnect.
- **Two devices fighting over the Crow** — only one Bluetooth connection at a time. Turn the
  other one's Bluetooth off.
- **A transmit or scan seems stuck** — every job has a Stop. If the app looks frozen, the
  Crow recovers on its own; worst case, power-cycle it.
- **Something's broken** — flash back to **v1.2.0-beta** from the web flasher and
  [open an issue](https://github.com/DaGromflomite/SleazyCrow/issues/new) with what
  happened.

---

## 18. The honest status (beta)

Every function here has been run on the bench with real radios. Two things still need a
specific target to fully prove, and both are waiting on hardware, not a fix:

- The **2.4 GHz spectrum view** needs a strong, close 2.4 GHz source to show up reliably on
  the bench.
- **MouseJack** needs a real vulnerable wireless dongle to show the catch-and-inject part.

And two features are marked beta until they meet the real world: the **car-follower
detector** on an actual drive, and the **key-fob next-code** against a real vehicle. Build
and bench tests pass; the road test is the last step, and that's yours to do.
