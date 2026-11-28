# Sleazy Crow

![Sleazy Crow](branding/source/banner.png)

**Sleazy Crow** is firmware and an Android app for the [Evil Crow RF V2](https://github.com/joelsernamoreno/EvilCrowRF-V2)
hardware — a pocket radio tool for sub-GHz and 2.4 GHz. It's built for reliability and adds its own features:
an "is a car following me?" tire-sensor detector, car key-fob decoding (now including Chrysler/Dodge/Jeep/RAM),
sub-GHz capture / replay / brute-force, a 2.4 GHz toolkit, and more.

> **Beta build (v2.3.5-beta).** A crash in the file-upload path is fixed, all twelve
> car key-fob decoders have been checked against real captured signals with known
> answers, and the key-fob next-code self-test now runs on a real captured signal
> instead of a made-up one. Every app function has been run on real radios on the
> bench (see [`docs/QA-STATUS.md`](docs/QA-STATUS.md)). It's still beta because the two
> features that need a real-world target haven't had one yet: the car-follower detector
> hasn't been tried next to a real car on a drive, and key-fob next-code hasn't been
> fired at a real vehicle. If something misbehaves, please
> [tell us what happened](https://github.com/DaGromflomite/SleazyCrow/issues/new).
>
> **Flash this one over USB with the web flasher, not over Bluetooth.** This build changes
> how the Crow talks to the app, and a Bluetooth (over-the-air) firmware update from an
> older version can leave the Crow in a bad state. Use the web flasher and a USB cable.
> The app itself still updates the normal way.
>
> New to it? Start with the [**instruction manual**](docs/MANUAL.md) — it walks through
> every function step by step.

## Install

- **Firmware:** the web flasher at **https://dagromflomite.github.io/SleazyCrow/** (desktop Chrome, Edge or Opera), or esptool with the files from [Releases](https://github.com/DaGromflomite/SleazyCrow/releases). Flash over USB.
- **App:** `SleazyCrow-vX.Y.Z.apk` from [Releases](https://github.com/DaGromflomite/SleazyCrow/releases). It installs as its own app next to the official EvilCrow RF app.
- **Updates:** the app updates itself from this repo's releases. For the firmware, use the web flasher over USB. The over-the-air (Bluetooth) firmware update is not offered for this build.

## What it can do

Everything below is driven from the phone app over Bluetooth. There's a step-by-step
walkthrough of each one in the [instruction manual](docs/MANUAL.md).

**Sub-GHz (300–928 MHz), two radios:**
- **RF scanner** — sweep the common bands and see which frequencies are busy, as a live
  spectrum or a list of decoded signals.
- **Record** — capture a signal off the air and save it as a `.sub` file.
- **Replay / transmit** — send a saved `.sub` back out, or send a raw on/off pattern on a
  frequency you pick.
- **Brute force** — walk the whole code space of a garage/gate protocol (CAME, Princeton,
  NICE, Holtek, FAAC, BFT and more), in order or as a de Bruijn sequence.
- **Key-fob reader (ProtoPirate)** — read a car key fob, show the make and button, and for
  the supported makes build and send the next valid rolling code. **Reading works for every
  supported make with no setup.** For next-code, the maker keys that are already public on
  GitHub (the decrypted KeeLoq/AUT64 lists) are now built into the firmware, so next-code is
  available out of the box for VW/Audi/Seat/Škoda (VAG), StarLine, Hyundai/Kia, Fiat and the
  big gate/alarm catalog, alongside the makes that need no key at all (Ford, Chrysler/Dodge/
  Jeep/RAM). You can still load your own keys for a make that isn't covered. Only use this on
  fobs and vehicles you own or have written permission to test. Full breakdown in
  [docs/PROTOPIRATE-SUPPORTED.md](docs/PROTOPIRATE-SUPPORTED.md).
- **Playlists** — line up several `.sub` files and send them in order with one tap.
- **Download library** — pull signals straight from the Flipper Sub-GHz DB and UberGuidoZ's
  collection to the SD card.

**Tire-pressure sensors (TPMS):**
- **"Is a car following me?" detector** — listen for the tire sensors on nearby cars and
  flag one that keeps showing up. Reads nine sensor types across the common US makes, shows
  make, pressure and temperature, and lets you save known followers.
- **One band at a time (315 MHz or 433.92 MHz).** A tire sensor only shows up if the Crow is
  tuned to the band it transmits on. Most North American cars use **315 MHz**; the exception
  worth knowing is **Jeep/Chrysler/Dodge/RAM (FCA), which use 433.92 MHz even in the US**, as
  do European cars. For US police vehicles that means Ford (Interceptor Utility) and Chevy
  (Tahoe PPV) are on **315**, while Dodge (Charger/Durango Pursuit) is on **433.92**.
- **Why it can't scan both bands at once.** The Crow has two radios, but they share a single
  data line to its main chip, and TPMS listening means pulling a non-stop stream of raw radio
  off a band and sorting the real sensor packets out of the noise in real time. Running that
  for both bands at the same time is more than the chip can keep up with — it falls behind and
  drops readings on both, so you hear less, not more. It was tried on the hardware and did not
  work, so there is no "both bands" mode; you pick the band that matches the car you're after.
  The manual has the full make-and-police-vehicle band guide.

**2.4 GHz (nRF24):**
- **Spectrum view** — see which 2.4 GHz channels are busy.
- **Jammer** — flood a band (Wi-Fi, BLE, Bluetooth, Zigbee, RC, a single channel, or the
  whole range). For testing on gear you own, where it's legal to do so.
- **MouseJack** — scan for vulnerable wireless mice and keyboards.

**Files and device:**
- **File manager** — browse, make, upload, rename and delete files on the SD card or the
  Crow's internal storage, over Bluetooth.
- **Settings** — radio options, hardware-button actions, nRF power/rate/channel, device name.
- **App update** — the app checks GitHub for a newer app version. Firmware for this build is installed over USB with the web flasher, not over Bluetooth.

## What's different from the original

See [`changelog.json`](changelog.json) for the full list. Highlights:

**New in this build (v2.3.5-beta).** This round was about the car key-fob decoders and a
crash in the file-upload path. What got fixed:

- **Fixed a crash when sending a signal file to the Crow.** A fast or interrupted upload
  could write past the end of a buffer and scramble memory the SD card code was using, which
  showed up later as a crash the next time the Crow opened a file. The upload now checks its
  own limits before every write, so a bad upload gets dropped instead of taking the device
  down. Run on the hardware with a deliberately abusive upload: it used to crash every time,
  and now it doesn't.
- **Every car key-fob decoder was checked against real captured signals.** Twelve makes, each
  one tested against a real recording with a known answer, and the serial the Crow reads back
  matches: Ford, Kia (five variants), Subaru, Suzuki, Fiat, VW/Audi/Seat/Škoda (VAG),
  Chrysler/Dodge/Jeep/RAM and PSA (Peugeot/Citroën). The ones that read back wrong were fixed
  to match the real signals.
- **The key-fob next-code self-test was failing on a made-up signal.** The Ford decoder itself
  was fine; the built-in test was feeding it a hand-written code that no real Ford fob sends,
  so it could never pass. Switched the test to a real captured Ford signal. The self-test now
  passes, and a real Ford fob was read correctly over the air to confirm it.
- **The app's "emulate" button now matches what the Crow can actually send.** The Crow tells
  the app, per signal, whether it can re-transmit it, instead of the app guessing from a
  fixed list. Signals the Crow can only read (not re-send) no longer show a send button.

**In v2.3.0-beta.** This round was about making the whole thing solid and
proving it on real hardware. What got fixed and added:

- **Car key-fob next-code now works for every supported make.** VAG Type 3/4 was the last one
  that read back wrong; it's fixed and checked on the hardware (all four VW/Audi/Seat/Škoda
  makes pass). Chrysler/Dodge/Jeep/RAM decoding was added and confirmed over the air, bit for
  bit. Details in [`docs/PROTOPIRATE-FORWARD-CODES.md`](docs/PROTOPIRATE-FORWARD-CODES.md).
- **The public maker keys are now built in.** The decrypted KeeLoq/AUT64 maker-key lists that
  are already public on GitHub ship with the firmware, so next-code works out of the box for
  StarLine, Hyundai/Kia, Fiat, the VAG AUT64 types and the gate/alarm catalog — not just the
  keyless makes. You can still load your own. Authorized testing only; see the manual's legal
  notice.
- **Fixed a memory leak that could make the tire-sensor detector go deaf.** After a long
  session the Crow's free memory slowly drained and, once it got low, the TPMS receiver
  stopped hearing anything until a reboot. Tracked it to the detector's decode task not
  releasing its buffers when it stopped; fixed, and confirmed with a soak test — memory now
  returns to its starting level after heavy use.
- **Bluetooth reconnect is much more reliable.** Connecting again right after the Crow reboots
  used to time out until you restarted the app. The app now clears the stale connection and
  retries on its own; tested across repeated reboots with no manual steps needed.
- **Chrysler/FCA now shows in the app's supported-makes list** (it decoded fine before, the
  badge was just missing).
- **Every app function was run on real radios** — capture, replay, jammer, brute force,
  scanner, key-fob decode (including an over-the-air read), tire-sensor detection — with a
  Flipper Zero providing signals and a HackRF confirming transmissions. Written up in
  [`docs/QA-STATUS.md`](docs/QA-STATUS.md).
- **Honest diagnostics.** The built-in boot self-test used to flag the transmitter as "partial"
  because it was judging the transmit with the second onboard radio, which can't time fast
  pulses accurately. The transmit timing was confirmed faithful off-air with a HackRF, and the
  self-test now reports it correctly.
- **New plain-English docs:** a [supported-fobs list](docs/PROTOPIRATE-SUPPORTED.md) and a
  full, beginner-friendly [instruction manual](docs/MANUAL.md).

Known limits in this beta: the tire-sensor follower detector hasn't been tried next to a real
car on an actual drive yet, and key-fob next-code hasn't been fired at a real vehicle — both
are proven on the bench, not in the field. The `.sub` transmit path is faithful for normal
signals; very large raw-sweep files that don't fit in memory fall back to a slower path with
slightly stretched timing (on the list to improve).

**New in v2.2.0-beta:**

- **Key-fob reader can build the next rolling code (for testing on vehicles you own
  or are allowed to test).** It reads the fob, moves the counter forward, re-encrypts
  it with a manufacturer key you load, and sends it. Working on StarLine and Fiat so
  far. It needs the key loaded first — capturing a signal doesn't reveal the key, and
  it won't guess it. There's a full write-up in
  [`docs/PROTOPIRATE-FORWARD-CODES.md`](docs/PROTOPIRATE-FORWARD-CODES.md).
- **Car-follower screen got a Clear button and a scroll bar** for long lists of
  tracked vehicles.

**Earlier in the v2 line:**

- **"Is a car following me?" detector.** The Crow listens for the tire-pressure
  sensors on cars around you and flags one that keeps showing up. It now reads 9
  sensor types, covering most common US makes (Ford, Toyota/Subaru, Hyundai/Kia,
  GM/Chevy, Honda, Nissan/Infiniti, Jeep/Dodge/RAM, plus Schrader and TRW). Each
  one was tested on the bench with real radio signals.
- **Car key-fob reader now supports Chrysler, Dodge, Jeep, and RAM.** Built and
  checked against a real 2019 RAM fob.
- **Fixed a serious bug:** the 2.4 GHz jammer could get stuck on and freeze both
  radios (including the car detector) until you unplugged the Crow. It stops
  cleanly now.
- **"Transmit Binary" works.** You can send a raw on/off pattern on a frequency you
  pick. The button used to do nothing.
- If the key-fob reader hears a signal it can't identify, it now saves it and shows
  "Unknown" instead of quietly dropping it, so you know it heard something.

**Earlier fixes (still here):**

- Bluetooth no longer drops ~12 s after connecting on Android.
- No boot crash on newer Arduino cores; toolchain pinned so every build is identical.
- **Sub-GHz `.sub` replay now transmits with faithful pulse timing.** Before, each
  pulse was held too long while the next one was read from the SD card, which
  stretched and garbled short-pulse signals — TouchTunes, for example, worked from
  a Flipper but did nothing from the Crow. Now the signal is loaded into RAM first
  and clocked out tightly. Verified over the air against a Flipper Zero: the Crow's
  transmission decodes bit-for-bit identical to the source file, with pulse widths
  within ~1%.
- File uploads can no longer silently save a corrupted file. (Every TouchTunes file
  copied to the SD card was also checked byte-for-byte against the source repo — all
  8,167 matched exactly.)
- **Download signals straight from the big community libraries.** The Sub-GHz
  downloader pulls from the Flipper Sub-GHz DB and **UberGuidoZ's** collection,
  including his ready-made playlists — pick exactly what you want and send it to the
  SD card. You can drill into **any** subfolder, not just the top-level categories.
- **File downloading to the SD card is fixed** — much faster and more reliable, and a
  download can no longer silently save a corrupted file (every file is checked against
  the source).
- **Build, edit, and save your own Sub-GHz playlists** — ordered lists of `.sub` files
  with pauses, so you can send a sequence with one tap.
- **Play a saved `.sub` right from the Sub-GHz screen.** A Play button is built in, so
  you no longer have to switch over to the Files tab just to transmit a file.
- Transmit toggle and brute-force frequency/timing options.

Feature documentation for the device itself is in Senape3000's original README: [`docs/UPSTREAM_README.md`](docs/UPSTREAM_README.md).

## Build

```
pio run                                   # firmware -> .pio/build/esp32dev/
cd mobile_app && flutter build apk --release
python tools/sleazy.py package --apk mobile_app/build/app/outputs/flutter-apk/app-release.apk
```

Releases are built by GitHub Actions from a version tag; see [`RELEASING.md`](RELEASING.md).

## Support

Sleazy Crow is free and open source. If it's useful to you and you'd like to help keep the work going, you can buy me a coffee:

☕ **[buymeacoffee.com/dagromflomite](https://buymeacoffee.com/dagromflomite)**

Totally optional — a star on the repo and good bug reports help just as much.

## Credits

- **Senape3000**: author of the [EvilCrowRF-V2](https://github.com/Senape3000/EvilCrowRF-V2) firmware and app this fork is based on.
- **Joel Serna (joelsernamoreno)**: Evil Crow RF V2 hardware design and original firmware.
- **tutejshy-bit**, **realdaveblanch**, **ProtoPirate**: see the credits in the original project.
- **DaGromflomite**: Sleazy Crow fork maintainer. The problems were diagnosed and the fixes directed by DaGromflomite; the code was written with AI assistance (Claude).

Not affiliated with or endorsed by Senape3000 or Joel Serna.

## License

MIT, see [LICENSE](LICENSE). Original work © 2025–2026 Senape3000; Sleazy Crow modifications © 2026 DaGromflomite.
