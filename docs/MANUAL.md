# Sleazy Crow — Instruction Manual

This manual walks through every function of the Sleazy Crow, step by step, and explains how
to actually get good results out of each one. It assumes you have never used a radio tool
before. If you already know this stuff, skip ahead — but the extra detail is here on purpose.

You drive everything from the phone app (Android). The Crow is the little board that does
the radio work; the app is the remote control and the screen.

---

## Read this first — what the Crow is, and the rules

The Crow is a two-way radio for the bands that garage remotes, car fobs, tire sensors,
weather stations, and 2.4 GHz gadgets live on. It can **listen** (receive and decode) and it
can **talk** (transmit). Listening is passive and legal almost everywhere. Transmitting and
jamming are not — they are regulated, and in most countries you need a licence or an
exemption to key up at all, and jamming is flat-out illegal.

So, the deal:

- **Only transmit on gear you own, or gear you have written permission to test.**
- **Know your local rules before you transmit or jam.** In the US that is the FCC; the EU,
  UK, Canada, Australia and most others have the same kind of law. Jamming is illegal in
  nearly all of them, with real fines.
- **This is a test tool.** It is for learning how these systems work, checking your own kit,
  and authorized security testing. It is not for messing with other people's things. What
  you do with it is on you.

There is a full legal notice at the end of this manual. Read it.

---

## The 10-minute first run

Do this once to get from a box to a working setup.

1. **Flash the firmware onto the Crow.**
   - On a desktop computer, open **Chrome, Edge, or Opera** (this does not work on a phone
     browser or on Safari/Firefox).
   - Go to **https://dagromflomite.github.io/SleazyCrow/**.
   - Plug the Crow into the computer with a USB cable. Use a real data cable, not a
     charge-only one — if nothing shows up, the cable is the first thing to suspect.
   - Click the flash/connect button on the page, pick the Crow's serial port when the
     browser asks, and let it run. It takes a couple of minutes. Do not unplug it mid-flash.
   - When it finishes, the Crow is running Sleazy Crow firmware.

2. **Put the app on your phone.**
   - On an Android phone, download `SleazyCrow-vX.Y.Z.apk` from the project's Releases page.
   - Open the file to install it. Android will warn you about installing outside the Play
     Store; allow it for this one file. The app lands on your home screen next to your other
     apps. It does not replace the official Evil Crow app — you can have both.

3. **Turn on Bluetooth and location on the phone.** Android needs location permission to scan
   for Bluetooth devices. That is an Android rule, not something the app does with your
   location — grant it or the Crow will not show up.

4. **Power the Crow.** USB power from a battery pack or a wall charger is fine. The onboard
   battery works too if yours has one.

5. **Connect.**
   - Open the app. The **Home** screen lists any Crows it can see.
   - Tap **Connect** next to yours (it is named "SleazyCrow" with a Bluetooth address).
   - Wait a few seconds. The top bar fills in — two antenna icons, the nRF icon, battery,
     free memory, temperature — and the status goes green: **Connected**.

That's it. You are ready. The rest of this manual is one section per feature.

**If the connect fails the first time:** the app now retries on its own, so give it a moment.
If it still will not connect, see the troubleshooting section — the usual fix is a quick
Bluetooth off/on on the phone, which clears an Android cache.

---

## The top status bar (what the little icons mean)

Once you are connected, this strip sits at the top of every screen. It is your health
readout — glance at it often.

- **Two antenna icons (1 and 2)** — the Crow has two separate sub-GHz radios. Grey means that
  radio is idle. Coloured means it is busy (recording, transmitting, scanning). Two radios
  means you can, for example, watch for car tire sensors on one while you do something else
  on the other.
- **nRF icon** — the 2.4 GHz radio (the one for Wi-Fi/Bluetooth-band work). It turns orange
  when it is transmitting, for example while the 2.4 GHz jammer is running.
- **Battery** — charge level, with a little bolt when it is on USB power.
- **Chip number (e.g. "37K")** — how much free memory the Crow has right now, in kilobytes.
  It drops while a job runs and climbs back when the job stops. That is normal. You do not
  need to watch it, but if it ever sits very low after everything is stopped, power-cycle the
  Crow to clear it.
- **Thermometer** — the Crow's own temperature. Only interesting if it is running hot.

---

## 1. RF scanner — find out what's on the air

**What it's for.** Before you can record or copy a remote, you need to know which frequency
it talks on. The scanner sweeps the common bands and shows you where the activity is. It is
also just a good way to see how busy the radio around you really is.

**How it works, plainly.** Radios talk on set frequencies, like stations on a dial. A garage
remote might be on 433.92 MHz; a US car fob on 315 MHz. The scanner watches a list of these
common frequencies and tells you which ones have a signal on them right now.

**Steps.**
1. On the **Home** tab, find the **Scanner** panel.
2. Pick which radio to use: **M1**, **M2**, or **1+2** to use both at once (both covers the
   band faster).
3. Choose a view:
   - **Spectrogram** — a bar for each frequency, rising with signal strength. Good for "is
     anything happening and roughly where".
   - **Signal List** — a running list of signals it managed to decode, with details. Good for
     "what exactly did it hear".
4. Tap the **play** button (top right). The status reads **Scanning active**.
5. Now make the thing you are curious about transmit — press the garage remote, the car fob,
   the doorbell. Watch which bar jumps. Colour shows strength: **red strong, orange medium,
   green weak**. The stronger the bar, the closer or more powerful the transmitter.
6. Tap the **stop** button (the red square) when you are done.

**Using it well.**
- Press your remote several times while watching. A single press is easy to miss. A bar that
  jumps every time you press is your frequency.
- "Strong" needs the transmitter fairly close. If you are across the yard from the gate, you
  will see a weaker, greener bar. Move closer to confirm.
- The scanner only knows the common frequencies in its list. An oddball device on an unusual
  frequency may not show. For those, the Record screen's frequency search (below) can hunt
  more finely.
- 2.4 GHz devices (Wi-Fi, Bluetooth) do **not** show here — that is a different radio. Use the
  nRF Spectrum screen for those.

---

## 2. TPMS — "is a car following me?"

**What it's for.** Every modern car's tires have a little sensor inside that broadcasts its
own fixed ID, plus the tire's pressure and temperature, every so often. Those IDs never
change for the life of the sensor. This screen listens for them and flags any car whose
sensors keep showing up near you across time and places — which is what following you looks
like on the radio.

**How it works, plainly.** Your car has four sensor IDs. So does the car behind you. If you
see the same four IDs at your house, then on the highway, then at the shop, that is the same
car in all three places. The Crow does the listening; the app does the "I keep seeing this
one" part and warns you.

**Steps.**
1. Tap the **TPMS** tab at the top of the Home screen.
2. Pick the **band** — the single most important setting, because a sensor only appears if
   you are tuned to the frequency it actually transmits on:
   - **315 MHz** — North America (US / Canada) and Japan.
   - **433.92 MHz** — Europe and most of the rest of the world.
   - **The one US exception worth knowing: Jeep, Chrysler, Dodge and RAM (FCA) use 433.92 MHz
     even in North America.** If you are watching for an American FCA vehicle, choose 433.92,
     not 315.
   - If you do not know, run one band for a while, then switch and run the other.

   **Which band for which make** (quick reference):

   | Band | Makes it covers |
   |---|---|
   | **315 MHz** | Ford (US), Toyota / Lexus / Subaru / Scion (US), Honda / Acura, Nissan / Infiniti, GM / Chevy (older), and most Schrader-equipped US cars |
   | **433.92 MHz** | **Jeep / Chrysler / Dodge / RAM (FCA — including in the US)**, Hyundai / Kia (VDO), BMW, Opel / Saab / Vauxhall, Fiat, Peugeot / Citroën, Renault, and European-market Ford / Toyota |

   > **About the "Both" button — a current hardware limitation.** The app also offers a
   > **Both** option that listens on 315 and 433.92 at once. On the current Evil Crow hardware
   > the two radios cannot both decode at the same time: in Both mode the Crow *scans* both
   > bands, but only the primary band (315 MHz) reliably turns signals into decoded vehicles.
   > **For dependable results, pick the single band that matches the car you are after** (use
   > the table above). True simultaneous dual-band decoding is a known limitation we plan to
   > revisit in a later release.
3. Set **Flag a follower when** — how eager the warning is. **Immediate** flags a car the
   moment it matches; the looser settings wait for more sightings before crying wolf.
4. Tap **Scan**. The radio starts listening. The line under it — **Radio: activity N (peak
   M)** — is your proof it is hearing RF at all. If activity stays flat at zero for a long
   time with cars around, something is off (try the other band, or restart the scan).
5. Each car that comes through appears as a card: the **sensor ID**, the **make** (and
   sometimes model family), the **pressure** and the **temperature**. A card turns red and
   says **FOLLOWER** when it matches your rule.
6. **Clear** empties the list and starts the tracking fresh. The **three-dot menu** has more,
   including saving your own car's sensor IDs as "known" so your own tires do not keep
   tripping the alarm.

**Using it well.**
- **This works best from a moving or recently-moving car.** Tire sensors mostly transmit when
  the wheels are turning or just stopped. A car parked for an hour goes quiet. If you scan
  from a desk, you will see little — that is the sensors sleeping, not a broken Crow.
- **Clear the list before you set off.** Otherwise the cars that were parked around you where
  you started look like followers for the first few minutes.
- **Teach it your own car.** Scan your own vehicle first, save those four IDs as known, and
  they stop setting off the warning. Now a flag means someone else.
- A single sighting is not a follower. The point is **repeats across trips and places**. Let
  it run over a real drive, not thirty seconds in a parking spot.
- Pressure and temperature are a bonus for identifying a specific car; the ID is the part
  that matters for tracking.

---

## 3. Sub-GHz: Record — capture a signal off the air

**What it's for.** Copy a remote's transmission so you can study it or send it again. This is
the bread and butter of a sub-GHz tool.

**Before you record, get two things right: frequency and preset.** A recording on the wrong
frequency is just noise. Use the RF scanner first (section 1) to find the frequency, or use
the **frequency search** on this screen (the magnifying-glass icon) — start it, press your
remote, and it locks onto the strongest frequency it hears. The **preset** is the modulation,
which is the "shape" of the signal. For most simple remotes **AM 270** (also written OOK)
is right. If a capture comes out as garbage, the preset is the usual culprit — try another.

**Steps.**
1. Tap **Sub-GHz** at the bottom, then the **Record** tab.
2. Pick **Module 1** or **Module 2**, and make sure the **Recording** toggle is selected (not
   Jamming).
3. Set the **Frequency** (for example 433.92) from the dropdown, and the **Preset** (AM 270
   for most). **Advanced** opens finer radio settings — bandwidth and data rate — which you
   only need for stubborn signals.
4. Tap **Start Recording**. The panel shows **Recording** with the live radio settings and a
   running edge count so you can see it is alive.
5. **Trigger the thing you want to copy** — press the remote, hold it near the Crow's antenna,
   press it a few times. Each captured burst appears under **Signals Captured** with a name
   like `m0_43392_AM_270_xxxx.sub`.
6. Tap **Stop Recording**.
7. Each capture has two buttons:
   - a **save** (disk) icon — keep it on the SD card for later, and
   - a **send** (arrow) icon — transmit it right back out (next section).

**Using it well.**
- Hold the remote within a few inches of the Crow for a clean, strong capture. Distance adds
  noise.
- Press the button three or four times. More copies of the same burst means a cleaner result
  and lets you confirm you caught the real thing and not a glitch.
- If you got nothing: wrong frequency (re-check with the scanner), wrong preset (try AM 650 or
  an FM preset), or the remote uses a rolling code that still records fine but will not simply
  replay (see the next two sections).

---

## 4. Sub-GHz: Replay / transmit — send a signal back out

**What it's for.** Play a saved signal out over the air. For a simple **fixed-code** remote
(many older garages and gates), replaying the recording works the device, because the code
never changes.

**Important — rolling codes.** Most modern car fobs and newer garage remotes use a **rolling
code**: the code is different every press, so a plain replay of an old press does nothing
useful. Replay is for fixed-code gear. For rolling-code car fobs, use **ProtoPirate**
(section 6), which can work out the *next* valid code for the makes it supports.

**Steps.**
1. On the Record tab, find your saved signal under **Signals Captured**, or load one from the
   SD card with **Play Sub File** (it opens a file browser — tap a folder to open it, tap a
   `.sub` file to load it).
2. Tap the **send** (arrow) icon on the signal.
3. A safety box appears. Read it, then **Tap to Transmit** to send, or Cancel to back out.
   (You can tick "don't show again" once you understand it.)
4. The Crow transmits the exact signal it recorded. The top status shows it briefly, then
   reads "Signal transmitted".

**Using it well.**
- Be in range of the thing you are testing. These are low-power radios; a garage across the
  street may not hear you, where ten feet away it will.
- If a replay does nothing on gear you own, it is almost certainly a rolling code — that is
  the device working correctly, not a Crow failure.
- You can also send a **raw on/off pattern** on a frequency you choose, from the transmit
  controls, if you know the timing you want.

---

## 5. Sub-GHz: Brute force — try every code

**What it's for.** Some older fixed-code garages and gates use a small, fixed set of possible
codes (set by little DIP switches inside the remote). Brute force walks through all of them
in order, so one of them opens the gate. This only works on **fixed-code** protocols with a
small code space. It does nothing against rolling codes.

**How it works, plainly.** If a remote has, say, 12 switches, there are 4096 possible codes.
Brute force sends all 4096, one after another. A **de Bruijn** sequence is a clever shortcut
that packs those codes together so the whole set goes out in much less airtime — use it when
the protocol allows, it is far faster.

**Steps.**
1. Tap the **Brute** tab.
2. Narrow the list by category (All, EU Garage, US Garage, Home Auto, Alarm) or search by
   name.
3. Pick **Standard** or **DeBruijn** at the top (DeBruijn where it is offered — it is quicker).
4. Tap the **play** arrow on a protocol: CAME, Princeton, NICE, Holtek, FAAC, BFT, and more.
   Each row shows its frequency, bit length, and an estimated run time.
5. A details box appears — frequency, timing (the "te", the base pulse width), delay between
   codes, number of repeats. The defaults are sensible. Tap **Start**.
6. A bar shows progress — **codes sent / total**. **Pause** and **Stop** are there the whole
   time.

**Using it well.**
- **Check the time estimate before you start.** A 12-bit protocol in de Bruijn is seconds to
  minutes. A large code space in standard mode can be hours. The estimate on each row tells
  you which you are in for.
- Stay in range of the gate for the whole run, or it will send all the codes to nobody.
- If nothing opens after a full run, it was probably the wrong protocol or a rolling code.
  Capture the real remote first (section 3) to see which protocol it actually is.

---

## 6. Sub-GHz: ProtoPirate — car key fobs

**What it's for.** Read a car key fob — show its make, which button was pressed, and its
rolling counter — and, for the supported makes, work out and send the **next** valid rolling
code. This is the one tool here built specifically for the rolling codes that plain replay
cannot handle.

**How it works, plainly.** A modern fob sends a different code every press, tied to a counter
that ticks up each time and is scrambled with a secret per-maker key. To make a code the car
will accept next, you need three things: the fob's serial, the current counter, and that
maker key. ProtoPirate reads the first two straight off the air. The maker key is the catch —
and the good news is below.

**Which makes work, and the keys.** Reading a fob — make, button, counter — works for **every
supported make with no setup at all**. For building the *next* code:

- **No key needed (keyless makes):** Ford, and Chrysler/Dodge/Jeep/RAM (FCA). These scramble
  with the counter itself, not a secret, so next-code works straight out of the box.
- **Key built in and public:** VW/Audi/Seat/Škoda on the VAG Type-2 format. Its key is public,
  so it is compiled in and just works.
- **Public maker keys now included:** the decrypted KeeLoq/AUT64 maker-key lists that are
  already published on GitHub are built into this firmware. That covers StarLine, Hyundai/Kia,
  Fiat, the VAG AUT64 types, and a large gate/alarm catalog — so their next-code is available
  out of the box too.
- **Loading your own:** if you need a make that is not covered, you can add its key — on the
  SD card at `/DATA/KEYS/keeloq_mfcodes.txt`, from the app's add-key screen, or compiled into
  a build. Reading a fob never reveals its key; the key has to come from you.

**Steps.**
1. Tap the **ProtoPirate** tab.
2. Pick the **frequency** — most fobs are **433.92** (EU) or **315** (US); 868.35 and 303.87
   are there for the rest. Pick the **module**.
3. Tap **Start Decode**. It reads "Decoding on module … at … MHz" and listens.
4. **Press the fob** near the antenna. If it is a make the Crow knows, a card shows the make,
   the button, and the rolling counter. If it is not, it shows **Unknown** with the raw data,
   so you still know it heard something real.
5. For a make that supports next-code, use the next-code control on the decoded card to build
   and send the following valid code. For the keyless and built-in-key makes this needs
   nothing extra; for a loaded-key make it uses the key you added.

**Using it well.**
- Press the fob once, cleanly, close to the antenna. A decoded card with a sensible counter
  means a good read.
- If you only ever see **Unknown**, you may be on the wrong frequency (try the other of 433.92
  / 315) or the fob is a make not in the list.
- **Authorized use only.** This builds working car-fob codes. Use it on fobs and vehicles you
  own, or that you have written permission to test. That is not a suggestion — it is the line
  between a security tool and a crime.
- One honest note for this beta: the decoders and keys are checked on the bench, but next-code
  has not been fired at a real moving vehicle yet. Treat it as "should work" rather than
  "proven in the field" until you have confirmed it on your own car.

---

## 7. Sub-GHz: Playlists — send several files in a row

**What it's for.** Line up a set of saved `.sub` files and send them back to back with one
tap — handy for a sequence you run often.

**Steps.**
1. Tap the **Playlist** tab.
2. Tap **New playlist**, give it a name, tap **OK**.
3. Add saved `.sub` files in the order you want them sent. Set a pause between them if you
   need one.
4. Play the list. The Crow sends each file in turn, waiting your pauses in between.

**Using it well.** Keep playlists short and purposeful. A long list fired blindly is just a
lot of airtime. Order matters — the files go in the order you set.

---

## 8. Sub-GHz: Jamming — flood a frequency

**What it does.** Blasts noise on a sub-GHz frequency so nothing else on it can be heard. On
the Record tab, flip the **Jamming** toggle (instead of Recording), set the frequency, and
start and stop it like a recording.

> **Warning — read this before you even think about it.** Jamming is illegal in most
> countries, including the US (FCC) and the EU, with serious fines. It does not politely block
> only your target. It can knock out **car key fobs, garage and gate remotes, alarms, medical
> and safety devices, and every other radio on or near that frequency, for everyone in
> range.** That can put people in danger. Only ever use it on equipment you own, in a space
> you control — a shielded bench or an RF-isolated room — and only where it is legal. Never in
> public, never on the road, never against anything you do not own. Everything that happens is
> your responsibility.

---

## 9. nRF: Spectrum — see the 2.4 GHz band

**What it's for.** The 2.4 GHz band is where Wi-Fi, Bluetooth, wireless mice, drones and a lot
of other gadgets live. This shows you which channels are busy.

**Steps.**
1. Tap **NRF** at the bottom, then the **Spectrum** tab.
2. Tap **Start Analyzer**. The nRF radio engages (its icon up top goes active).
3. Bars show busy channels across 2.400 to 2.525 GHz.
4. **Stop** when done.

**Using it well.** The nRF radio is not a sensitive lab instrument — it shows strong, nearby
activity well, but faint or distant signals may not register. It is a "what's loud around me"
view, not a precise spectrum analyzer. For that you would use an SDR.

---

## 10. nRF: Jammer — flood a 2.4 GHz band

**What it does.** The same idea as sub-GHz jamming, on 2.4 GHz. **The same warning applies in
full** — it is illegal in most places, and it knocks out Wi-Fi, Bluetooth, and other 2.4 GHz
devices for everyone nearby, not just your target. Only on gear you own, in a space you
control, where it is legal.

**Steps.**
1. Tap the **Jammer** tab.
2. Pick a **mode**: Full Spectrum, Wi-Fi, BLE Data, BLE Advertising, Bluetooth, USB Wireless,
   Video Streaming, RC Controllers, Single Channel, Custom Hopper, Zigbee, or Drone. The gear
   icon on each sets its options (for Single Channel, the exact channel; for Custom Hopper,
   the range and step).
3. The **Dwell Time** slider sets how long it sits on each channel before hopping — all the
   way left is **Turbo** (hop as fast as possible). Shorter dwell covers more channels; longer
   dwell hits each one harder.
4. Tap **Start Jammer**. The nRF icon up top goes orange.
5. Tap **Stop Jammer** to end it.

**Using it well (on your own bench).** Full Spectrum spreads the effort thin across the whole
band; Single Channel concentrates it on one frequency, which is far more effective against a
single device. Pick the mode that matches what you are testing.

---

## 11. nRF: MouseJack — scan for vulnerable wireless mice and keyboards

**What it's for.** Some wireless mice and keyboards use 2.4 GHz dongles with a known weakness
("MouseJack"). This scans for those devices.

**Steps.**
1. Tap the **MouseJack** tab.
2. Tap **Start Scan**. It sweeps the 2.4 GHz channels looking for those devices; a progress
   bar shows it working.
3. Anything it finds lists under **Targets**. **Hide Unknown** filters out ones it cannot
   identify.
4. **Stop Scan** to end.

**Using it well.** There is nothing to find unless a real vulnerable dongle is powered and in
range — an empty list usually means "nothing vulnerable nearby", not a broken scan. Use it to
check your own wireless desk gear.

---

## 12. Files — manage the SD card over Bluetooth

**What it's for.** Browse and organize what is on the Crow without plugging it into a
computer.

**Steps.**
1. Tap **Files** at the bottom.
2. Switch between **SD Card** and **Internal** storage at the top.
3. Tap a folder to open it. The breadcrumb at the top (Root / … ) and the **Up** button walk
   you back out.
4. The toolbar: **refresh**, **new folder**, **upload** (send a file from the phone to the
   Crow), and **multi-select** (tick several files to rename or delete at once).

**Good to know.** Captured signals live in `/DATA/SIGNALS`. Key files for ProtoPirate go in
`/DATA/KEYS`. The `.sub` files you record and transmit are plain text — the same format a
Flipper Zero uses.

---

## 13. Settings

Tap **Settings** at the bottom. Each row expands.

- **SDR Mode** — turns the Crow into a plain spectrum/raw receiver over USB, for use with
  computer SDR software. Leave it off for normal app use.
- **App Settings** — language, cache, and permissions for the app itself.
- **RF Settings** — the defaults the other screens start from: brute-force delay, repeats and
  transmit power; radio power per module; scanner options. Raising transmit power gives range
  but uses more battery; the top setting is fine for most work.
- **HW Buttons** — choose what the Crow's own physical buttons do when it is not connected to
  the phone.
- **nRF24 Settings** — the 2.4 GHz radio's power level, data rate and channel.
- **Firmware Update** — shows the firmware version on the Crow (see next section).
- **Device Management** — rename the Crow (handy if you have more than one), and other device
  options.

---

## 14. Firmware update (over Bluetooth)

1. In **Settings**, open **Firmware Update**. It shows the version currently on the Crow.
2. Tap **Check FW Version**, then **OTA Update**.
3. The app checks the project's GitHub releases. If there is a newer firmware it offers to
   flash it over Bluetooth; if not, it says **No new version available**.
4. **Do not unplug or walk away mid-flash.** Let it finish.

The app also tells you on its own when a newer firmware or app version is out.

---

## 15. If something goes wrong

- **Won't connect / times out.** The app now retries on its own, so wait a few seconds first.
  If it still fails, turn the phone's Bluetooth off and back on, then reconnect — that clears
  an Android cache. If it is really stubborn, close and reopen the app. This is an Android
  Bluetooth quirk, not a Crow fault; the device is fine.
- **Two devices fighting over the Crow.** Only one Bluetooth connection at a time. If you want
  the PC connected, turn the phone's Bluetooth off first, and the other way round.
- **A transmit or scan seems stuck.** Every job has a Stop button — use it. If the app looks
  frozen, the Crow recovers on its own; worst case, power-cycle it (unplug and replug power).
- **A capture is garbage, or a replay does nothing.** Wrong frequency or wrong preset on a
  capture; a rolling code on a replay (that is the device working as designed). Re-check the
  frequency with the scanner, try a different preset, and remember rolling codes need
  ProtoPirate, not plain replay.
- **Really broken.** Flash back to an earlier firmware from the web flasher, and
  [open an issue](https://github.com/DaGromflomite/SleazyCrow/issues/new) with what happened —
  what you did, what you expected, and what it actually did.

---

## Legal notice and terms of use

**Read and understand this before you transmit anything.**

This firmware, app, and documentation are provided for **education, research, and authorized
security testing only**. By using the Sleazy Crow you agree to the following.

1. **You are solely responsible for how you use it.** The maintainer and contributors provide
   this as-is, with no warranty of any kind, and accept no liability for how you use it or for
   any damage, loss, or legal consequence that follows.

2. **Receiving (listening) vs transmitting.** Listening to and decoding radio signals is legal
   in most places. **Transmitting and jamming are heavily regulated** and are illegal without
   the proper authority almost everywhere. Jamming in particular is a criminal offence in the
   US, the EU, the UK, Canada, Australia, and most other countries, and carries large fines.

3. **Only operate on equipment you own or are authorized in writing to test.** Using this
   against vehicles, remotes, networks, or devices you do not own or have explicit permission
   to test may be a crime — including unauthorized access, theft, interference with radio
   communications, and more — and may endanger people who rely on the systems you disrupt.

4. **Know and follow your local law.** Radio law differs by country and by band. It is your
   job to know the rules where you are, including any licence you need, before you key up.

5. **The included maker keys.** The car-fob maker keys built into this firmware are ones
   already published in the open. They are included so the tool works for authorized testing,
   the same way a lock-picking set is sold for locksmiths and hobbyists. Including them is not
   permission to use them against property that is not yours. Do not.

6. **No malicious use.** Do not use this to steal, to damage, to interfere with safety or
   medical devices, or to harm, harass, or spy on anyone.

If you do not accept these terms, do not use the Sleazy Crow.
