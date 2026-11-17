# Sleazy Crow v2 — Changelog & Feature Documentation

**Status:** in development on branch `dev` (released line `v1.2.0-beta` is frozen on `sleazycrow`/`main`).
This document is the authoritative, running record of **everything changed and every feature added**
for the v2 release, so v2 can ship fully documented. It is updated as work lands — keep it current.

Baseline: everything below is on top of the published **v1.2.0-beta** firmware + app.

---

## VAG Type 3/4 decode fixed — full RF roundtrip (2026-11-17)

**Authorized security testing only.** The last incomplete ProtoPirate item —
VAG Type 3/4 (500µs, AUT64) — now does a full encode → forward(+1) → decode RF
roundtrip, **verified on real hardware** (ESP32-PICO-D4 bench Crow), 4/4 for
every real type byte 0xC0–0xC3 (VW/Audi/Seat/Skoda).

- **Root cause:** the type 3/4 data path reused the custom Manchester state
  machine written for type 1/2. The 500µs sync framing carries an *implied
  leading `1` bit* and needs the exact Flipper `manchester_advance` FSM; the
  custom machine decoded that convention wrong (≈50% of bits), so sync/data never
  recovered cleanly. (Diagnosed against upstream `protocols/vag.c` and Flipper
  `lib/toolbox/manchester_decoder.c`, then proven with a faithful offline model
  over 20,000 random frames before touching firmware.)
- **Fix:** type 3/4 now decodes with a verbatim port of Flipper's
  `manchester_advance` (`PPVag::manchAdvanceFlipper`); type 1/2 is untouched.
  Adjacent same-level half-bits merge on-air into one long pulse, which that FSM
  handles as a valid full-bit event.
- **Self-test:** the on-device `forwardSelfTest` VAG line now **gates** on type
  3/4 (`t3/4 RFP`), and the bench boot self-test (`esp32dev-selftest`) gained a
  dedicated `[T-VAG]` section that runs the roundtrip for all four types.
- **Known limit (same as upstream):** a type-0x00 frame (MSB=0) can't roundtrip
  because the framing fixes the implied first bit to `1`; real 500µs frames carry
  a 0xC0–0xC3 type byte, so this does not affect live use.

---

## Post-2.1.0 unstable — ProtoPirate forward rolling-code generation (2026-11-17)

**Authorized security testing only.** Full guide: `docs/PROTOPIRATE-FORWARD-CODES.md`.

Restores ProtoPirate to full utility on the Evil Crow hardware: it can now
synthesize the **next valid rolling code** (advance the counter, re-encrypt with
a loaded manufacturer key, transmit) for authorized penetration testing against
vehicles/fobs the operator owns or is permitted to test.

- **Central keystore** (`pp::Keystore`) shared by every rolling-code decoder.
  Keys load from the SD file `/DATA/KEYS/keeloq_mfcodes.txt` (`name:learning:hexkey`)
  at boot, and testers can add keys live from the app. No keys are hard-coded;
  the operator supplies the key for the make they're authorized to test.
- **Forward-code engine** on the protocol base class (`supportsForward()` +
  `generateForward()`), with two reference implementations:
  - **StarLine** (KeeLoq): decrypt hop with the device key → advance the counter
    field → re-encrypt. Verified.
  - **Fiat V0** (cleartext counter): bump the counter in the frame. Verified.
  - **KIA V3/V4** (KeeLoq): rewired off its hardcoded key onto the shared
    keystore; added the missing TX encoder; decrypt → advance counter → re-encrypt.
    Verified (crypto + the new encoder round-trips our own decoder).
- **Module + BLE commands** `0x0C`–`0x11`: emulate-forward, add/clear/reload/count
  keystore keys, and an on-device self-test.
- **On-device self-test** (`0x11`): builds a synthetic frame with a known key,
  advances the counter, and checks it decodes to counter+1 — proving the compiled
  firmware's cipher and forward path with no RF. Result on hardware:
  `StarLine 65→66 PASS | Fiat 43981→43982 PASS | ALL PASS`.
- **App:** a **"Transmit NEXT code (rolling)"** button on rolling-code results
  (counter-advance picker + authorized-use notice), plus keystore add/reload.
- **Verification:** KeeLoq cipher proven a true inverse over 100,000 random pairs
  and the StarLine encode→decode→forward→decode pipeline over 20,000 cases
  (host Python model), then confirmed on real hardware via the self-test.
- **Ford V0:** the forward **math** is implemented and self-tested (advance the
  20-bit counter, re-apply the reversible XOR + byte-swap + GF(2) CRC; it decodes
  back to counter+1 with a valid CRC). But Ford's Manchester TX encoder has a
  **pre-existing preamble bug** (it disagrees with its own decoder), so Ford
  forward can't transmit a valid frame until that's fixed against a real Ford
  capture. Not guessed.
- **VAG** has no encoder in our port (stubbed "behind a feature flag") and its
  AUT64 keys are never loaded, so forward needs the upstream VAG encoder ported
  and a real fob to validate — held rather than guessed.
- Subaru/PSA/KIA V5-V6 are decode-only. See the table in the forward-codes guide.

---

## v2.1.0-unstable — release summary (2026-11-16)

Published to the `v2-unstable` channel. The headline work this round, all tested on real
hardware with real radio signals (Flipper transmitting, HackRF witnessing, a real car fob):

**TPMS "is a car following me?" detector**
- Expanded from 5 to **9 tire-sensor decoders**: added Hyundai/Kia, Honda (marker-anchored),
  TRW, Jeep/Dodge/RAM (FCA), and Nissan/Infiniti (preamble-anchored), alongside the existing
  Ford, Toyota/Subaru, Schrader and GM/EG53. Covers most common US makes. Each was verified on
  the bench by transmitting the real sensor frame from a Flipper and confirming the Crow + app
  read the correct ID, pressure and temperature, with zero ghosts and no cross-make leakage.
- **Phantom-sensor fix (anchored demod).** Weak 8-bit checksums let a wrong bit-alignment
  "validate" as a real sensor (a deterministic shift-ghost), so the detector could show a car
  that wasn't there. The decoder now locks onto the leading constant run + preamble and decodes
  one alignment, gated on a physically plausible pressure/temperature. 0 ghosts / 0 misses over
  16,000 synthetic frames, then confirmed on hardware across all 9 protocols and a mixed scene.
- App: scroll bar on the sensor list; new makes show their name.

**Transmit Binary (0x06) — implemented**
- Was a dead stub that returned success and transmitted nothing (the low-level `transmitBinary`
  never entered TX mode). Now parses `[freq][pulseDuration][bits]`, keys the CC1101 via the
  proven `setTxWithPreset(Ook650)` + `transmitRawData` path. Verified on a HackRF: it decoded
  the exact key 0x123456 from a pattern the Crow sent (+40 dB at the commanded frequency).

**ProtoPirate (car key-fob decoder)**
- **New make: Chrysler / Dodge / Jeep / RAM (FCA).** 112-bit Manchester, te≈209 µs,
  reverse-engineered from a real 2019 RAM 1500 fob and confirmed **live on-device** (decodes to
  a steady serial, 0xC6B2ED23). New `PPChryslerFca.h`, registered in `PPAllProtocols.h`.
- A captured signal with no matching decoder is now **saved to the SD and shown as "Unknown"**
  instead of being silently dropped (`saveRawCapture` + an "Unknown (no decoder)" result).
- **Load-.sub bug fix:** the offline loader did `if (duration_us > 0)` and dropped every LOW
  pulse, feeding decoders a mangled half-signal. Now stores the absolute value of all pulses.
  (The live decode path is fully working; the offline load path still needs more verification.)

**Reliability**
- **Flash reliability:** pinned `upload_speed = 115200` — the default 460800 was too fast for the
  board's USB and failed mid-write with a "flash chip" error.
- **nRF jammer wedge fix** — see the next section (critical).

---

## CRITICAL FIX — nRF jammer wedged the radios / couldn't be stopped (2026-11-16)

**Severity: showstopper (found & fixed during full-device QA with HackRF + BLE).**

- **Bug:** `NrfJammer::jammerTask` runs at FreeRTOS priority 2 pinned to core 1.
  In turbo mode (dwell=0, the default for every mode) the loop body never blocks,
  and the loop only called `taskYIELD()`, which yields to equal-priority tasks
  only. The BLE command / Arduino-loop task that sets `stopRequest_` is LOWER
  priority on the same core, so it was **completely starved** the moment the
  jammer started.
- **Impact:** once started from the app, the jammer **could not be stopped over
  BLE**, and because the jam task holds the shared **HSPI mutex** (`acquireSpi`),
  it **wedged both the nRF and the CC1101 radios — including TPMS follower
  detection — until a physical power-cycle.**
- **Evidence (HackRF/PothosSDR + BLE):** during jam `getState` got **0/8**
  responses; `0x2B` stop produced no ack and no `running=0`; a follow-up nRF init
  reported **present=NO**. (An early "+20 dB" emission reading in the WiFi band
  was a **false positive from ambient WiFi** — caught by re-testing in a
  WiFi-immune guard band.)
- **Fix** (`src/modules/nrf/NrfJammer.cpp`): replace the `taskYIELD()` with
  `vTaskDelay(1)` every 48 turbo iterations, briefly **blocking** the jam task so
  the command task runs and can set `stopRequest_`. ~1 ms idle per ~7 ms jam →
  ~85% duty retained, and the jammer is now reliably stoppable.
- **Re-verified on hardware:** `getState` **8/8** during jam; stop acks +
  `running=0`; nRF recovers; **6/6 arm/stop cycles clean, no wedge**; and the
  **CC1101/TPMS RX restarts after a jam cycle** (core feature survives).

---

> **REMOVED FEATURE — read first.** An earlier round built a phone-radio **detection suite**
> (a Detect tab, Fox Hunt, and a signature database that flagged trackers / surveillance gear). It was
> **removed entirely** (commit `e0bb08e`) because phone radios couldn't reliably see the targets. It is
> **not in the shipping app** and is not a Sleazy Crow feature. Sections further down that describe it are
> kept only as a dated development log of what was tried — do not quote them as current features.

## Headline features in v2

1. **The Crow is optional for browsing** — every menu is browsable offline; only hardware functions wait for the device.
2. **Sub-GHz transmit playlists** — build, save, and play ordered lists of `.sub` files with pauses.

---

## 1. Detection suite — REMOVED (not a feature)

The phone-radio detection suite (Detect tab, BLE scanner, Fox Hunt, signature database, classic-BT and
drone detection) was prototyped across several rounds and then **removed in full** (commit `e0bb08e`):
phone radios couldn't reliably detect the intended targets, so shipping it would have given users false
confidence. The screens, the signature JSON, and the detection tests are gone from the app. The dated test
rounds later in this file mention it only as a record of the attempt; it is **not** in the product.

---

## 2. Navigation — nothing locked without a Crow

- Removed the "Connection Required" gate (and the "nRF not detected" gate) from the bottom navigation.
  **Every tab is browsable with no device attached** — you can peruse all menus and build/edit playlists
  offline. The Crow-only functions (transmit, record, SD browse, NRF ops) still guard themselves and
  prompt to connect, so nothing is *usable* without the device — the tabs just aren't blocked.
- Completes the "run without a Crow" intent for Sub-GHz / NRF / Files that P0 only did for Detection.
- Files: `lib/screens/home_screen.dart`.

---

## 3. Sub-GHz transmit playlists

A new **Playlist** tab in the Sub-GHz menu — build and transmit ordered lists of saved `.sub` files,
reachable straight from Sub-GHz without going to the Files tab.

- **Entries are files or pauses.** Each playlist entry is either a `.sub` **file** (path + pathType) or an
  explicit **pause** of a chosen length (presets 250 ms → 10 s), so signals can be spaced right in the builder.
- **Playback** (`startPlaylist` / `stopPlaylist`): transmits entries in sequence, self-paced on the firmware
  burst-complete (`SignalSent`) event — the same reliable gating as hold-to-repeat. Supports **per-file
  repeat**, an **inter-file gap** slider, explicit **pause** entries, and whole-list **loop**. Stop is
  interruptible mid-pause. Pins one CC1101 module so bursts never contend. **No firmware change.**
- **Editor/player UI:** reorderable entries (drag handle), delete, add `.sub` files via an **SD browser**
  (pathType 5), **Add pause**, gap / repeat-each / loop controls, a live current-entry highlight while
  playing, and a **Save** button (+ tap-title-to-rename). Building works fully offline.
- **Storage — phone-side, with export:** playlists are saved **on the phone** (JSON in SharedPreferences),
  so they can be built/edited offline and need no firmware. A per-playlist **Export to SD** writes a
  `.json` into a `/PLAYLISTS` folder on the Crow's SD card (Crow must be connected) for backup/portability.
  A **first-run info dialog** explains this storage model.
- Files: `lib/models/sub_playlist.dart`, `lib/services/playlist_service.dart`,
  `lib/screens/playlist_screen.dart`, `lib/screens/subghz_screen.dart`,
  `lib/providers/ble_provider.dart` (playlist player).

---

## 4. Design / theming

- Cyberpunk **pink→purple** gradient identity on the Playlist screens.
- **Logo watermark:** blank areas show a very subtle, desaturated Sleazy Crow emblem instead of a flat
  black wall — on the main Connect screen (disconnected) and the playlist library and editor (empty, and
  faintly behind a short list). Reusable `LogoWatermark` / `LogoEmptyState`.
  Files: `lib/widgets/logo_watermark.dart`, `lib/screens/home_screen.dart`,
  `lib/screens/playlist_screen.dart`.

---

## 5. Testing

First automated tests in the project (`flutter test`), all passing:
- `test/sub_playlist_test.dart` — 5 tests over the playlist model (file vs pause entries, JSON round-trip,
  file counting, defaults).
  _(The detection-signature tests were removed with the detection suite — see section 1.)_
- `flutter analyze`: 0 errors (only pre-existing info-level lints).

---

## Hardware test round 1 — fixes (2026-11-05)

From the first on-device test pass:
- **Fox hunt usable** — removed the "calibrating" gate (you can't move closer/farther to a device you
  can't find); strength + warmer/colder now show immediately (median + EMA + auto-range still apply).
- **Detect noise control** — a **"Flagged only"** toggle hides unflagged devices (TVs/phones/unknowns),
  and a **"Surveillance & tools only"** preset in the filter sheet (one tap: keep surveillance/drone/tool/
  skimmer, drop trackers + iBeacon) for users who only care about surveillance/LE/hacker gear.
- **Keep screen awake** setting (Settings → Connection, **default ON**) — holds a wakelock so the screen
  doesn't sleep and drop the BLE link mid-transmit (addresses GATT-timeout / FBP-code-6 disconnects).
- **Delete captured signals** — the Record list's long-press menu now has **Delete** (was View/Save/
  Transmit only); the delete path already existed, it just wasn't exposed in local/record mode.
- **AirTag** relabel/regrade (see Detection) and **watermark** brightened (now ~0.10 opacity).
- Playlist **Loop** confirmed OFF by default.

**Still open from round 1:** ProtoPirate decode is broken (confirmed). Connected, the file picker opens,
but it won't read/parse/decode files — a real functional break inherited from upstream. Scope unknown
(app parse path vs firmware decode module) until a live `adb logcat` repro with the Crow connected.
May need a firmware fix/rebuild. Deferred per user's time. Pinned.

## TouchTunes replay investigation + deep-folder picker (2026-11-13)

Reported: TouchTunes `.sub` files (UberGuidoZ `Sub-GHz/TouchTunes/…`) that work from a Flipper do
**nothing** when replayed from the Crow. Suspected corrupt downloads or antenna tuning.

**Ruled out — it is not the download path or file corruption.**
- The app saves each `.sub` to SD **verbatim** (`resp.bodyBytes`, no re-encode/line-ending rewrite);
  the only transform (`splitIfLargeRaw`) triggers only above `MAX_UPLOAD_BYTES` (124968 B) and
  TouchTunes raw files are a few KB — never split.
- Confirmed `TouchTunes/raw/Vup.sub` is `433920000` / `FuriHalSubGhzPresetOok650Async` / `Protocol: RAW`.
  Firmware `RawProtocol::parse` + `StreamingPulsePayload` rebuild pulses by the **sign of each number** —
  identical to Flipper. The decode is faithful.

**Leading Crow-side TX suspects (need on-hardware confirmation):**
1. Auto-selected module/antenna port — file replay auto-picks the first idle CC1101 (module 0).
   (User reports both ports have identical stock antennas, which weakens this.)
2. **Per-board frequency offset not applied on the file-TX path** — `BRUTER_CC1101_FREQ_OFFSET 0.052`
   is used by the bruter path but `setTxWithPreset`→`setMHZ(433.92)` applies no trim.
3. `holdPulse` quantizes long LOW gaps to the 1 ms FreeRTOS tick (`CC1101_Worker.h`) — minor jitter.
**Dual-radio self-test — BUILT & RUN on hardware (COM15, 2026-11-13).**
New bench build `env:esp32dev-selftest` (`-DSELFTEST_ON_BOOT=1`) → `src/modules/selftest/SelfTest.{h,cpp}`,
hooked in `main.cpp setup()` after radio init / before the worker+BLE start, prints over serial then halts.
Flashed to the bench Crow, captured, then the release build (`esp32dev`) was reflashed to restore it.
Results:
- **T1 radio liveness:** both modules program 433.92 MHz and read it back at 433.9198 — SPI + synth OK.
- **T2 pulse-timing fidelity:** 40-pulse pattern (incl. the long LOW gaps that hit `holdPulse`'s
  `vTaskDelay` path), expected 134591 µs, measured 134653 µs = **+0.05%**. **Timing is faithful —
  rules out the `holdPulse` suspect (#3).**
- **T3 TX→RX keying:** module-0 carrier vs floor on module 1 = **+54 dB @315, +60 dB @433.92,
  +42 dB @868**. **The TX chain radiates and keys OOK cleanly — the transmit hardware is healthy,
  not dead.**
- **T4 relative frequency:** peak at 433.920, **+0 kHz between the two radios** — but this cannot see a
  *shared* absolute error (both crystals off by the same amount reads as 0). The config's own
  `BRUTER_CC1101_FREQ_OFFSET = 52 kHz` (prior SDR-measured trim) is the evidence that the absolute
  error is real.
- **T5 PA response:** inconclusive — the two radios sit ~2 cm apart so module 1 saturates at ~-13 dBm
  regardless of PA. Not a failure.

**Interim conclusion (superseded below):** TX timing/keying/chain all good; suspected absolute crystal offset.

### ROOT CAUSE FOUND — SD-streaming TX stretches every pulse (2026-11-13)

Added **T7 (file→antenna fidelity)** to the self-test: write a TouchTunes-shaped `.sub` to SD, replay it
through the **real** parse+stream+TX path (`StreamingSubFileParser` + `StreamingPulsePayload`) on radio 0,
capture the RF on radio 1 with an edge-timing ISR, compare to the file. Captured the frames + both
inter-frame gaps (RF structurally present) but a **16–18% TX overrun** and only 46% per-pulse match.

A/B isolation nailed it:
- **[A] `StreamingPulsePayload.next()` = ~228 µs PER PULSE** of overhead (measured iterating it with no TX).
- **[B] the identical pattern replayed from an in-RAM array = +0.1%** (tight, faithful).
- Streaming TX of the same file = **+16.3%**.

**Mechanism:** the transmit loop is `while (payload.next(dur,lvl)) { digitalWrite(pin,lvl); holdPulse(dur); }`.
`next()` does on-demand SD/String work costing ~228 µs, during which **the pin holds the previous level** —
so **every pulse is lengthened by ~228 µs.** For a TouchTunes capture (pulses ~100–900 µs) that is a 25–230%
per-pulse distortion: the waveform is garbled and the jukebox's receiver can't decode it. A Flipper (and the
Crow's own in-RAM TX path) clock pulses out with no such gap, which is exactly why the same file works there.
The RAM-saving streaming path (added for multi-MB RAW sweeps) silently broke timing for normal `.sub` files.
**Frequency/crystal offset was a red herring** — plausible but not the cause.

**Fix (designed, not yet built):** pre-load the pulse list into a RAM vector and transmit via the existing
in-RAM `transmitData(PulsePayload&)` path (proven +0.1%) when it fits — with a heap/size guard that falls
back to streaming only for giant files that won't fit (free heap ≈112 KB; a max 122 KB RAW file's pulses
exceed that, so the guard matters). Bench-verifiable with the same T7 (expect the real path to drop to ~+0.1%).
All of this stays local/unflashed to any daily device until verified.

Self-test artifacts: `env:esp32dev-selftest` (`-DSELFTEST_ON_BOOT=1`), `src/modules/selftest/SelfTest.{h,cpp}`,
hook in `main.cpp` after `CC1101Worker::init()` / before `start()`. Bench Crow was restored to `esp32dev`.

**All-paths audit (2026-11-13):** verified `transmitSub` was the *only* transmit path that streamed pulses
per-pulse from SD. Every other path already builds the full pulse list in RAM before keying it out, so none
had the stretch and none needed the change:
- `transmitRaw` (string→vector), `transmitRawData` (vector), `transmitBinary` (bits→vector),
  `transmitData(PulsePayload&)` — all RAM-sourced, tight.
- bruter `sendPulse` — RAM-generated codes, separate subsystem.
- jamming — continuous carrier, not pulse replay.
- ProtoPirate `loadSubFile` — uses `StreamingSubFileParser` to *decode* (feed decoders), never drives the TX
  pin, so per-pulse latency is irrelevant.
No code change made to those paths (don't churn working code). The fix lives where the bug was.

**New app feature — drill-down folder picker for the Sub-GHz downloader.**
- The downloader's "Choose Sections" dialog now **browses the Sub-GHz tree level by level**
  (breadcrumb + folders, tick to grab a whole subtree, arrow to go deeper) instead of only offering
  top-level folders. You can pull just `TouchTunes/raw`, or a single `pin/<code>`, etc.
- Selection model is now **folder prefixes at any depth**, kept minimal/non-overlapping, and is
  **backward compatible** — an old top-level-only saved selection still resolves (a name is a prefix
  of its whole subtree). Tri-state checkboxes show whole/partial/none; a folder covered by a selected
  ancestor shows ticked-and-locked.
- Files: `lib/services/flipper_subdb_service.dart` (`folderOf`, `folderCovers`, `browseFolder`,
  `SubFolderNode`, prefix-based `filterSelection`), `lib/screens/settings_screen.dart` (browser UI).
- Tests: `test/folder_selection_test.dart` — 17 cases (browse counts/depth, prefix coverage, brute-force
  exclusion, no double-count, legacy top-level selections). All pass; `flutter analyze` clean (info lints only).

## Hardware test round 3 — UberGuidoZ playlists, ProtoPirate import, TX reboot fix (2026-11-08)

Session on real hardware (Pixel 8 Pro + Crow on USB/BLE). **Firmware 1.2.5-dev, app 2.0.0-dev.**

**New app features**
- **Import UberGuidoZ's ready-made playlists** — the Sub-GHz downloader now lists Uber's 30
  `subplaylist/` playlists; ticking one downloads exactly the files it needs (from any folder,
  split pieces expanded, Uber's own broken entries reported) and adds it to the Playlists screen.
- **ProtoPirate "Load .sub" uses the folder browser** — the old whole-card list capped at ~50 files,
  so downloaded collections were invisible. Now browses the card folder-by-folder (shared
  `SdSubPicker` widget, also used by playlists and the new Play Sub File button).
- **Play Sub File** button on the Sub-GHz Record screen — pick any `.sub` on the card and transmit it
  without going through the Files tab.

**Critical firmware fix — TX watchdog reboot (dropped BLE / killed dual-radio)**
- A long RAW transmit (e.g. the 13 s Tesla sweep, or a De Bruijn brute sweep) busy-waited on **core 0**
  — the same core as BT and the watchdog-watched IDLE0 — so the task WDT rebooted the Crow mid-transmit
  and dropped the BLE link. Root-caused from the decoded backtrace (sub-GHz transmit loop).
- Fix: `holdPulse()` sleeps through long LOW gaps instead of busy-waiting the whole file; and the three
  long RF tasks (CC1101Worker, bruter_atk, PPDecode) are pinned to **core 1** via
  `xTaskCreateStaticPinnedToCore` so an RF busy-wait starves only the unwatched IDLE1. App side: the
  playlist now waits for the module to report Idle before sending the next file.
- **Verified on hardware:** record on module 2 (433.92) while transmitting the long Tesla file on
  module 1 (315) captured a live key-fob press with **no reboot** — dual CC1101 capture+transmit intact
  (capture is ISR-driven; TX never disables interrupts).

**Critical firmware fix — ProtoPirate file-load crashes**
- Loading a `.sub` into ProtoPirate crashed two ways (found on hardware): a **stack overflow**
  (`loadSubFile` opens via FATFS + runs every decoder inline on the small SerialCmd/BleInbound task),
  and a **`bad_alloc` abort** (a 100 KB RAW file grew the sample vector past free heap).
- Fix: run the load on a dedicated 12 KB `PPFileLoad` task (`loadSubFileAsync`) so it can't overflow
  the caller regardless of BLE/serial origin; cap samples at 8192; swallow exceptions; SerialCmd stack
  3 KB→8 KB.
- **Verified on hardware:** loading an Isuzu fob `.sub` via the app's folder browser over BLE decoded
  it (Kia V3/V4 family — serial, button, ENC, CRC) with no crash; the large Tesla RAW file that aborted
  before now loads cleanly.

**Detection (Flock / Axon / Taser / WiFi path) — confirmed wired**
- The WiFi scanner feeds the detection engine end-to-end (`detect_screen` → `evaluateWifi`). 19 categories
  incl. flock, axon (covers Taser), alpr, bodycam, ring, shotspotter, fleet — Flock & Axon have both BLE
  (manufacturer/service) and WiFi (OUI/SSID) rules.
- `detection_matching_test.dart` (19 tests) verifies the WiFi path against the real `signatures.json`
  (Flock OUI `b4:1e:52` + SSIDs, Axon SSIDs `axon-`/`ab4-` + OUI `00:25:df`, ALPR/bodycam/ring/shotspotter
  OUIs) plus no-false-positives. BLE manufacturer/service matching is structurally verified; full BLE
  match needs physical devices (user's upcoming field test).

**Quick SD format — works in the field (user-confirmed); only the PC-tethered test rig misbehaved.** In the
normal setup (Crow powered by the phone) the quick format works fine per the user. During the 2026-11-08 PM
automated test the Crow was on the **PC's** USB with the serial logger holding the port while the Pixel was
separate; there, `quickFormatSD()` (`f_mkfs`) returned false and **fell back to the slow Phase-2b per-file
delete** (app stuck on "Formatting…" 5+ min). Attributed to the test rig — the SD re-init after `SD.end()`
differs when the Crow isn't powered by the phone — not a field bug. Still worth a glance during physical
testing to confirm the fast path: read the Crow USB serial during a format — `f_mkfs result 0 … Quick format
done` = fast path OK; `Deleted N items` = slow fallback; `did not mount` = card lost. Both format entry
points (Settings, and the downloader "Format SD card first") hit the same firmware path.
Also found: the Sub-GHz **jammer runs continuously by design** — `maxDurationMs` is a cooldown-then-resume
duty cycle (overheat protection), NOT a stop; it jams until an explicit Stop Jam. (A smoke test that
assumed a short maxDuration would auto-stop left a jam cycling — test bug, not firmware.)

**Automated test sweep (all green)**
- 55 Flutter unit tests pass (detection, uber playlists, sd catalog, upload limits, format wait, …).
- Firmware feature smoke test over serial — GET_STATE, frequency search, PP live decode start/stop,
  PP file list, bruter start/stop, jam (auto-stop): **no reboot/crash** on any.
- App UI crash-sweep — all 12 screens/sub-tabs render with no Flutter exceptions, app stays running.
- Firmware builds clean (RAM 26.1%, Flash 57.8%).

## Hardware test round 2 — fixes (2026-11-07)

From the second on-device test pass. **Shipped in this build (1–3):**
- **Filter button is now a clear text toggle** — the Detect header filter was an icon-only button that
  wasn't readable in the field; replaced with a labelled **"Filter: ON / Filter: OFF"** button (pink-filled
  when on). File: `lib/screens/detect_screen.dart`.
- **Audible detection alert** — a short two-tone beep (+ heavy haptic) fires the **first time a device is
  flagged** in a scan session (once per device, not per RSSI update). Two independent **Settings → Detection**
  toggles: **"Surveillance alert sound"** (default **ON** — cameras / body cams / ALPR / Flock) and **"Tracker
  alert sound"** (default **OFF** — AirTag/Tile/SmartTag, which are common and noisy). Other severities
  (tools/drones/skimmers) don't sound. Files: `assets/sounds/detect_alert.wav`, `lib/services/alert_sound.dart`,
  `lib/providers/settings_provider.dart`, `lib/screens/settings_screen.dart`, `lib/screens/detect_screen.dart`;
  dep `audioplayers`.
- **"Scanning" message moved above the logo** — the empty-state status text ("Scanning for signals… / No
  signals raised yet") now sits **above** the watermark instead of layered over it. File:
  `lib/screens/detect_screen.dart`.

- **.sub source → UberGuidoZ (browse-by-folder).** Repointed the downloader off `Zero-Sploit/FlipperZero-
  Subghz-DB` to **`UberGuidoZ/Flipper`**, scoped to the **`Sub-GHz/` tree only** (the Crow can't use his NFC/
  IR/BadUSB/etc.). Can't whole-zip his repo (~2 GB monorepo; GitHub only zips whole repos), so the acquisition
  is now **browse-then-download**: one Git Trees API call lists every `.sub` under `Sub-GHz/` (11,290 files /
  156 MB, paths+sizes, **no bulk download**), the user picks top-level folders, and only those files are
  fetched individually from `raw.githubusercontent.com` (per-segment URL-encoded; verified against paths with
  spaces/commas). His **folder structure is preserved verbatim** under `SUB Files/` on the SD, so path-based
  playlists stay resolvable. Resume now persists the folder selection (not a cached ZIP). File:
  `lib/services/flipper_subdb_service.dart` (rewritten), `lib/screens/settings_screen.dart` (clone dialog
  flow + text). Done.

### P4 — WiFi scanner (Flock / ALPR / camera detection) — DONE
- **Root cause of the field-test miss:** the app was **BLE-only**, but modern Flock hardware is detected
  over **WiFi** (OUI `B4:1E:52`, `flock-` / `Flock Camera Net` SSIDs) and Flock has reportedly disabled
  Bluetooth on newer units — so standing next to one produced nothing.
- **Added a WiFi access-point scanner** (`wifi_scan` plugin) that runs alongside the BLE + classic-BT
  scans on the Detect screen. New permissions: `ACCESS_WIFI_STATE`, `CHANGE_WIFI_STATE`,
  `NEARBY_WIFI_DEVICES` (API 33+; `ACCESS_FINE_LOCATION` already present covers 10–12).
- **Engine:** `DetectionRule.matchesWifi(ssid,bssid)` + `DetectionCategory.evaluateWifi` +
  `DetectionProvider.evaluateWifi` light up the already-staged `wifiOui`/`wifiSsid` rules (64 across the
  DB — Flock, ALPR, Ring, camera, body-cam, etc. now fire). WiFi APs show in the Detect list with their
  own tile (SSID · BSSID · WiFi) and the same graded badge; surveillance hits trigger the audible alert.
- Files: `lib/services/wifi_scanner.dart` (new), `lib/detection/detection_models.dart`,
  `lib/providers/detection_provider.dart`, `lib/screens/detect_screen.dart`,
  `android/app/src/main/AndroidManifest.xml`, `pubspec.yaml` (dep `wifi_scan`).
- **Caveats (surfaced, not bugs):** Android throttles WiFi scans to ~4 / 2 min, so WiFi detections refresh
  slower than BLE (scan loop ~25 s). iOS has no public WiFi-scan API, so the scanner no-ops there (guarded).

### P4b — Crow WiFi promiscuous sniffer — ATTEMPTED then REMOVED 2026-11-11 (hardware can't support it)
**Outcome: REMOVED.** Real-hardware BLE testing proved the ESP32-PICO-D4 can't run a WiFi promiscuous
sniffer alongside NimBLE + the full firmware: idle free heap is only ~15 KB and `esp_wifi_init()` needs
~40-50 KB (`ESP_ERR_NO_MEM`, verified on-device; trimming WiFi buffers didn't help — init fails before the
buffers). The feature was fully reverted from firmware + app. Removing the linked WiFi stack recovered
**free heap 15 KB → 51 KB** and **flash 81% → 58%**. KEPT: the phone's own WiFi AP scanner (phone hardware)
+ signatures. Lesson recorded: verify feasibility on real hardware before building. Original (now-removed)
design below, for the record:

### (removed) original sniffer design — 2026-11-11
- **Field-test correction:** a **deployed** Flock camera is NOT visible to the phone's WiFi AP scan above.
  It backhauls over **LTE** (no persistent WiFi AP to scan), and Flock disabled the BLE beacon — so the
  phone sees nothing. The only live signal is the camera's **802.11 management frames** (probe requests /
  beacons) carrying a Flock MAC OUI, which require **monitor/promiscuous mode** the phone's OS never exposes.
  The ESP32 can do it. (Signatures also bumped to **v9**: added phone-visible `flockos`/`flocksafety`/
  `pigvision` SSIDs + `flockcam` name for setup-mode / older BLE-beaconing units.)
- **Firmware:** new `WifiSniffer` module — ESP32 WiFi promiscuous RX, channel-hops **1–13** (~120 ms dwell),
  hardware-filters **management** frames, extracts source MAC + SSID + RSSI, de-dups (LRU of 64 MACs,
  re-reports a MAC at most every 8 s or on a ≥12 dB jump), and streams each unique sighting to the app.
  Runs on **core 1** with the RX callback feeding a FreeRTOS queue, so the NimBLE host task on core 0 never
  stalls — WiFi/BT **coexistence** keeps the app link up (a little less snappy while scanning). Passive,
  receive-only. New commands **0x70 start / 0x71 stop / 0x72 status**; new messages **MSG_WIFI_HIT (0xD8)**
  and **MSG_WIFI_SCAN_STATUS (0xD9)**. Build clean (Flash 80.9 %).
  Files: `src/modules/wifi/WifiSniffer.{h,cpp}` (new), `include/WifiCommands.h` (new),
  `include/BinaryMessages.h`, `src/core/ble/ClientsManager.h`, `src/main.cpp`.
- **App:** a **"Flock" toggle** on the Detect header (shown only when the Crow is connected) starts/stops the
  sniffer and **holds the screen awake** while scanning (for in-car / walking detection). Crow sightings
  merge into the Detect list through the **same `evaluateWifi` engine** (so Flock OUI/SSID updates stay in the
  hosted signatures — no reflash), BSSID-deduped against the phone's own WiFi. Surveillance hits fire the
  existing audible/haptic alert. Files: `lib/providers/firmware_protocol.dart`,
  `lib/services/binary_message_parser.dart`, `lib/providers/ble_provider.dart`, `lib/screens/detect_screen.dart`.
- **Still to do:** background foreground-service (P5) so it alerts with the screen off / app backgrounded —
  the real "plug into the car, turn on, forget, get alerted" detector; on-device flash + field test; optional
  GPS/wardrive logging (deferred). Until P5 lands, the sniffer runs while the Detect tab is open.

### SD card format — both paths now wait for the Crow to finish (2026-11-08)
- **Bug:** the downloader's **"Format SD card first"** option started the format, waited a fixed **3 seconds**,
  then started downloading. On a full card the Crow can still be deleting files after 3 s, so the wipe could
  race the new downloads and delete them. The **Settings → Format SD Card** button had a fixed **30-second**
  timeout that reported "no response" while a big card was still being wiped.
- **Fix:** the app now waits for the Crow's own "format finished" message on both paths
  (`BleProvider.waitForSdFormat()`, backed by a completer; up to 15 minutes, since the Crow deletes files one
  at a time). If the Crow disconnects mid-format the wait ends at once as a failure instead of hanging.
  The downloader shows the live "Deleting: …" progress and **stops with a clear error** if the format fails,
  disconnects or never finishes, instead of downloading onto a half-wiped card.
- No firmware change: the firmware already sent progress and a final result; the app just wasn't waiting for it.
- Tests: `test/sd_format_wait_test.dart` (6 cases: progress isn't the end, success, failure, dropped link,
  no answer, long format not cut off). Files: `lib/providers/ble_provider.dart`, `lib/screens/settings_screen.dart`.

### Connect errors + WiFi permission (2026-11-08)
- **Wrong label:** any status containing "error" showed a red **"Permission Error"** card, so a plain connect
  timeout looked like a permission problem. The card now says **"Connection problem"** unless the message is
  really about a permission. File: `lib/screens/home_screen.dart`.
- **Clearer timeout message:** a connect timeout now says the Crow didn't answer and what to check (switched on,
  close by, not connected to another phone/app, power-cycle it), instead of "Bluetooth not ready".
  Connect timeout raised from 10 s to 15 s. File: `lib/providers/ble_provider.dart`.
- **Missing permission request:** the app declared **Nearby WiFi devices** (Android 13+) but never asked for it,
  so the WiFi detector (Flock/ALPR/cameras) could stay blind. It's now requested at startup with the Bluetooth
  and location permissions (optional; Bluetooth and the Crow work without it).

### Full-app bug sweep (2026-11-08)
Whole app + firmware reviewed (not just v2), with tests and an emulator pass through every screen.
- **Uploads over ~122 KB were silently truncated (upstream bug).** The Crow's upload chunk counters are one
  byte, so past 255 chunks (124,968 bytes) the counter wrapped and the Crow saved a cut-off file while
  reporting success. 239 of UberGuidoZ's 11,290 `.sub` files are bigger than that (mostly Gates/Garages brute
  force). The app now refuses an upload it can't deliver whole (`FirmwareBinaryProtocol.MAX_UPLOAD_BYTES`),
  and the downloader splits any RAW file over the limit into ~100 KB pieces (was: only files over 200 KB, into
  128 KB pieces, which were still too big). Verified losslessly on real UberGuidoZ files up to 3.2 MB (every
  piece fits, parses as a valid `.sub`, and the samples join back exactly). The finish message now also says
  how many files failed to download or were too big.
- **Connecting while Detect was scanning:** classic-Bluetooth discovery (Detect tab) blocks BLE connections on
  Android. Connecting now stops any scan and pauses discovery first; the Detect tab no longer starts scans
  after you've left it; the fox hunt pauses classic discovery (it disturbed RSSI).
- **Trackers could be hidden:** `BLUETOOTH_SCAN` was declared `neverForLocation`, which makes Android filter
  some BLE beacons out of scan results. Removed (location is requested anyway).
- **Service-UUID matching was substring-based**, so a short UUID like Tile's `feed` could match any 128-bit
  UUID containing "feed". Now exact, full 128-bit comparison.
- **"Keep screen awake" was switched off after OTA or a download** (they called `disable()` at the end).
  They now release only their own temporary wakelock.
- **Playlist files that fail are no longer skipped silently** (errors are muted during playback). At the end
  the app says which files didn't play, e.g. because they're no longer on the SD card.
- **Native classic-BT receiver** can no longer crash if Bluetooth Connect permission is revoked.
- Files: `ble_provider.dart`, `firmware_protocol.dart`, `flipper_subdb_service.dart`, `settings_screen.dart`,
  `detect_screen.dart`, `hunt_screen.dart`, `classic_bt.dart`, `detection_models.dart`, `settings_provider.dart`,
  `ota_screen.dart`, `MainActivity.kt`, `AndroidManifest.xml`. Tests: `test/upload_limits_test.dart`.

### Firmware 1.2.1-dev — fast SD format (2026-11-08) — NOT YET FLASHED/TESTED ON HARDWARE
- **Quick format:** "Format SD Card" now rebuilds the card's FAT filesystem in one pass (`f_mkfs`) instead of
  deleting every file one at a time, which took 10+ minutes on a full card and could end with the Crow
  dropping the Bluetooth link. Falls back to the delete method if the quick format can't run.
- **Delete fallback fixed for deep folders:** the recursive delete kept every parent folder open; the SD
  library allows only 5 open files, so trees like `SUB Files/Gates/Bruteforcing/...` failed to delete. It now
  holds no folder open while going deeper.
- **Upload path bounds check** (a 255-byte path could overrun a 256-byte buffer).
- Files: `include/FileCommands.h`, `src/core/ble/BleAdapter.cpp`, `include/config.h` (1.2.1-dev).

### Build playlists without the Crow — offline SD list (2026-11-08)
- The phone now keeps its own list of the `.sub` files on the Crow's SD card (`lib/services/sd_catalog.dart`),
  updated automatically: every successful upload adds its file (so the UberGuidoZ downloader fills it), every
  folder listing from the Crow replaces what the phone knew about that folder (corrects deletes/moves made
  elsewhere), and a successful format empties it.
- **Playlist → Add from SD with no Crow connected** now browses that saved list (header says how old it is)
  instead of refusing. If the phone has never seen the card it says how to fill the list.
- Because the card can change while the phone isn't looking, playlists report any file that fails to play.
- Tests: `test/sd_catalog_test.dart`.

### Firmware 1.2.1-dev flashed to the user's Crow (2026-11-08 13:15)
- Full 4 MB flash backed up first (`D:\Backups\SleazyCrow\crow-flash-backups\`, copy on Nextcloud); flashed
  over USB (COM15), all checksums verified; boots as "Sleazy Crow v1.2.1-dev". NVS settings + LittleFS kept.
  Quick format still to be tried on hardware.

**In progress / next (5):**
- **Crow disconnects during playlist playback** — needs a BLE keep-alive during inter-file gaps and/or
  mid-playlist auto-reconnect+resume (keep-screen-awake alone isn't enough).
- **UberGuidoZ playlist compatibility (to verify)** — keep the phone-side playlist feature; separately check
  whether his repo ships Flipper `.txt` playlists and, if so, translate their `/ext/subghz/...` paths to the
  Crow's `SUB Files/` layout + confirm the firmware can play that format.

### Over-the-air updates from OUR repo — app self-update fixed + auto-checks (2026-11-12)
**Scope (user decision):** this ships *inside* v2 and works from v2 onward (later published as v2.0.0-unstable, see below).
Crows on older firmware and phones on the old v1.2.0-beta app web-flash / sideload v2 once; after that,
updates arrive over the air.

**Update source = `DaGromflomite/SleazyCrow` GitHub releases only.** Verified in code (`UpdateService.githubOwner/githubRepo`)
and by tests on real release JSON: our assets (`sleazycrow-fw-vX.Y.Z[-beta]-OTA.bin`, `SleazyCrow-vX.Y.Z[-beta].apk`,
each with `.md5`) are picked; Senape's (`evilcrow-v2-fw-…`, `EvilCrowRF-v….apk`) never match, even when mixed in.
Senape appears only in Credits and the firmware attribution string.

- **Firmware OTA (Crow over BLE)** — already pulled from our releases + MD5 check + BLE transfer. New: the OTA screen
  now checks our releases **automatically** as soon as it knows the Crow's firmware version (no button tap needed).
- **App self-update — was broken, now fixed.** The old code launched the installer with `Process.run('am start … file://…')`
  (apps can't run `am`; `file://` URIs are blocked since Android 7) and wrote the APK straight into `/storage/emulated/0/Download`
  (blocked by scoped storage). Replaced with:
  - native `sleazycrow/installer` platform channel in `MainActivity.kt`: `canInstall`, `openInstallSettings`
    ("Install unknown apps" page for Sleazy Crow), `installApk` (system installer via a FileProvider `content://` URI);
  - FileProvider `${applicationId}.updates` → `<cache>/updates/` (`res/xml/update_file_paths.xml`, manifest);
  - APK downloaded into the app cache with progress, **streamed MD5 verified against the release's `.apk.md5`**,
    size checked, file deleted on any mismatch; stale updates cleared first;
  - flow: changelog dialog → download (progress) → verify → permission guide if needed → system installer (user confirms).
  - Android only accepts the update if it is signed with the Sleazy Crow key. Verified: our live
    `SleazyCrow-v1.2.0-beta.apk` and the current dev build share cert SHA-256 `9ff56c56…5207af`.
- **Launch-time check:** the app quietly checks for a newer app release at most every 12 h (no UI unless one exists).
- Refactor: app-update flow moved from `settings_screen.dart` to `lib/services/app_update_flow.dart`
  (Settings button + launch check share it); `UpdateService.pickLatest()` is now one pure, tested function for both checks.
- Files: `android/app/src/main/kotlin/com/sleazycrow/app/MainActivity.kt`, `android/app/src/main/AndroidManifest.xml`,
  `android/app/src/main/res/xml/update_file_paths.xml`, `lib/services/update_service.dart`,
  `lib/services/app_update_flow.dart` (new), `lib/screens/settings_screen.dart`, `lib/screens/home_screen.dart`,
  `lib/screens/ota_screen.dart`, `test/update_service_test.dart` (new, 8 tests), `test/fixtures/*_releases.json` (new).
- **Verified:** 40/40 tests pass; `flutter analyze` 0 errors; signed release APK builds and contains the provider,
  `com.sleazycrow.app.updates` authority and `REQUEST_INSTALL_PACKAGES`; live release asset MD5s match their `.md5` files.
- **NOT yet verified on a phone:** the actual download → permission → system-installer hand-off (needs a phone + a newer
  release than the installed app to test end-to-end). Firmware BLE transfer code is unchanged by this work.

### TPMS reception fix — road test heard nothing (2026-11-14)
First real road test (1 hr on I-90) picked up **zero** sensors. Code review found that capture could never deliver a packet:
- **Burst detector waited for 100 ms of silence.** In CC1101 async RX with no squelch, GDO0 toggles on noise non-stop,
  so a burst was never "complete" and the ISR buffer filled with noise first. **Replaced with a streaming segmenter:**
  the ISR buffer is drained every 10 ms and the pulse stream is cut into runs whose widths are whole multiples (1–8×)
  of the TPMS half-bit (52 µs FSK / 120 µs OOK, ±40 %). Runs of ≥48 pulses go to the decoder; the half-bit is refined
  per run from the single-period pulses (absorbs sensor crystal error).
- **ISR pulse floor 50 µs dropped jittery 52 µs symbols**, breaking the level alternation the Manchester demod relies
  on. Per-module floor `CC1101Worker::minPulseUs[]`: 12 µs while TPMS runs, restored to `MIN_PULSE_DURATION` on stop
  (other modes unchanged).
- Status `reserved` byte now = radio edges/256 over the last 2 s (0 = radio delivering nothing). `packets` now counts
  TPMS-shaped runs.
- **Verified:** Python simulation of the same segmenter+demod: 200/200 jittered Ford frames found buried in noise at
  both 52 and 120 µs, 0 false runs in 1M noise pulses. Firmware builds (RAM 27.0 %, flash 58.3 %).
  **NOT yet verified on the Crow or a car.**

### TPMS over-the-air verification + decoder fixes (2026-11-15)
First real RF test: the Crow's **radio 1 transmits a synthetic TPMS frame** (uploaded `.sub`) while **radio 0 runs the
TPMS scanner**. Pulse widths were dumped over serial (`-DTPMS_PULSE_DEBUG` build flag, off by default) and replayed
through a Python copy of the decoder.
- **Reception proven:** the 2026-11-14 segmenter catches real bursts (OOK and FSK, 110–118 pulses per frame).
- **Fix — last bit lost:** the half-bit after the final pulse merges into the idle line, so a frame ending in a `1` lost
  its last bit (Ford checksum byte always failed). Demod now appends that trailing half-bit.
- **Fix — wrong Manchester phase:** the decoder now tries both half-bit phases (a burst's first frame sometimes locked
  to the wrong one). FSK frames decoded: 0/20 → 17/20 on the captured data.
- **Fix — fake sensors:** a corrupted frame fell back to a random "generic fingerprint" ID → a fake sensor (and a
  false follower alert in *immediate* mode). Generic (no-CRC) IDs now need the identical ID twice within 15 s, and are
  held 1 s and dropped if a CRC-validated frame arrives in the same burst.
- **App:** TPMS screen gets a **315 / 433.92 MHz band switch** (remembered; retunes live) and a **radio-activity line**
  (activity / peak / decoded packets; red warning if the radio delivers nothing), so a 0-hit drive can be diagnosed.
- **Fix — checksum false positives (round 2):** brute-forcing every bit alignment against 8-bit checksums let garbled
  frames pass now and then (OTA: a wrong Schrader ID with a valid CRC; an FSK frame "passing" GM EG53). Now **every**
  ID — validated or generic — must repeat within 15 s, and decoders only run on their own modulation (FSK frames →
  Ford/Toyota/Hyundai, OOK → Schrader/GM).
- **Fix — degenerate IDs (round 3):** replaying every OTA capture showed weak checksums (Ford additive sum) "validating"
  preamble/garbage IDs like `0x55555555`, `0x000003FF`, `0x80000000` — and garbage repeats identically, so the repeat
  rule can't stop it. `tpms::plausibleId()` now rejects IDs that are periodic (period 1–4, ≤1 stray bit) or one run
  ≥ 2/3 of the ID width, for every protocol and the generic fingerprint. False-reject on random real IDs: 3 per
  million (32-bit), 24/M (28-bit), 169/M (24-bit).
- **Phone end-to-end verified (Pixel, 2026-11-16):** app connects, TPMS scan at 315 shows `Radio: activity ~153`,
  0 false packets; demo sensor shows `0x12345678 · Ford · 31.0 psi · 15°C · FOLLOWER`. History cleared after.
  Note: if the PC (bleak) was just connected, Windows holds the BLE link and the phone gets GATT 147/0x3E until the
  Crow is reset — not a field issue.
- **FOC:** TPMS FSK now writes FOCCFG explicitly (`TPMS_FOCCFG`, default 0x16 = library default). FOC off (0x14) lost
  433 MHz FSK completely; 0x1D no better.
- **Verified OTA on the final build:** Ford FSK 315 → exactly one hit `0xABCDEF01` Ford (20/20 frames decode);
  Schrader OOK 315 and 433.92 → exactly one hit `0x0AB1C2D3` Schrader; no fake sensors in any run. Self-test 0x9F
  pass; no crash; GetState fine after.
- **Known gap — FSK at 433.92:** frames arrive with lopsided pulse widths (radio-to-radio crystal offset; an RX
  sweep showed the best point ~+10 kHz and still marginal). 0 decodes, 0 fakes. Affects only FSK sensors on 433
  (EU cars, some newer FCA); US 315 FSK and all OOK are fine. Could be the Crow's own two crystals — needs a real
  433 sensor to judge.
- **Note (upstream bug, test-only impact):** the built-in `FuriHalSubGhzPreset2FSKDev238Async`/`Dev476Async` register
  tables are identical copies with GFSK @ ~3.8 kbaud and no DEVIATN entry, so .sub files using them don't transmit
  the FSK they name. The OTA test used a Custom 2-FSK 19.2 kbaud preset instead. Not fixed yet.
- **Still open:** a real car (only synthetic frames so far); Toyota differential-Manchester decode.

### Unstable channel + v2.0.0-unstable test release (2026-11-13)
User decision: publish v2 now as an **opt-in UNSTABLE** build with a disclaimer; stable v1.2.0-beta stays the default.
- **Version 2.0.0-unstable** (firmware ints 2.0.0, app build 40). `tools/sleazy.py set-version X.Y.Z --unstable`;
  `VER_RE` accepts `-unstable`.
- **App opt-in:** Settings → Updates → "Offer unstable updates" (default OFF; enabling shows a disclaimer).
  `UpdateService.pickLatest(..., includeUnstable:)` skips `-unstable` assets unless opted in; picks are flagged
  `unstable`; the app-update dialog and OTA screen label them UNSTABLE and require "I understand" before download/flash.
  Old v1.2.0-beta apps can't see unstable assets (their filename patterns only allow `-beta`) — covered by a test.
- **Web flasher:** `sleazy.py package` builds an UNSTABLE page for `-unstable` versions: big warning, the full
  human-readable release notes (`docs/RELEASE-NOTES-v2.0.0-unstable.md`, rendered to HTML — tested vs untested,
  grouped), and Install / Download APK locked until the disclaimer box is ticked. Published at `/unstable/` on the
  flasher site; the stable main page only gains a link.
- **Release notes:** `docs/RELEASE-NOTES-v2.0.0-unstable.md` — every v2 addition/change grouped (install/updates,
  TPMS, playlists, Sub-GHz/ProtoPirate, SD/files, general app, removed, known issues), each marked Works / Untested /
  Known issue / Removed. `changelog.json` has 2.0.0 firmware + app entries.
- **Verified:** 44/44 tests; analyze 0 errors; firmware builds (RAM 26.9 %, flash 58.1 %); **flashed to the Crow
  (COM15, flash backed up first): boots "Sleazy Crow v2.0.0-unstable", advertises over BLE**; signed APK 2.0.0-unstable
  (versionCode 40, cert 9ff56c56…); `sleazy.py check` passes; unstable page rendered and checked in Edge.

## Scope decisions (recorded)

- **Removed Meshtastic / LoRa detection** — out of scope; drone detection is the only mesh/RF-beacon target kept.
- **Signature sourcing stays MIT-clean** — re-derived from primary public sources, GPL lists used only as
  pointers.
- **Playlists are phone-side** (not an SD-only format) so they work offline; SD export is opt-in.

---

## Pinned for later (not yet in v2)

Roadmap: **P4** WiFi scanner (lights up the staged ALPR/Ring/camera/body-cam/hacker signatures) ·
**P5** passive background alerts (opt-in; includes a **"alert on surveillance-tool detection"** setting —
notify when Flock/Axon/ALPR/bodycam/ShotSpotter/camera/Ring/fleet/drone is seen; only meaningful once
background scanning exists) · **P6** Sub-GHz fox hunt *(firmware)* · **P7** polish + release.
**★ TPMS vehicle tail-detection — "is a car following me?" (flagship dual-radio idea, pinned 2026-11-08).**
Passive sub-GHz counter-surveillance: each car's 4 tire-pressure sensors broadcast a unique fixed ID, so
every nearby vehicle has an RF fingerprint. **Best justification on the board for TWO CC1101s** — TPMS is
315 MHz (N. America) + 433.92 MHz (EU/Asia); radio A parks on 315, radio B on 433.92, covering both bands
at once (a one-radio tool misses half the cars). The phone's GPS+clock make "following" computable: if the
same TPMS IDs reappear across multiple GPS points while you drive, that car is behind you. Reuses the P5
persistence/`followingScore` anti-stalking engine (applied to TPMS IDs instead of BLE addresses) + the
existing CC1101 OOK/FSK decode infra (ProtoPirate/FrequencyAnalyzer/ProtocolDecoder); TPMS protocols are
openly documented in rtl_433. Purely passive RX (defensive framing). Caveats: TPMS transmits mostly while
moving (~60–90 s) — ideal for the follow-in-traffic threat case; short range (tens of m) — fine for a tail.
Backups pinned with it: **dual-radio "capture trap"** (radio A detects a burst firing, radio B already
parked records it in full — kills the single-radio scan-vs-capture problem) and **sub-GHz wardriving**
(GPS+timestamp heatmap of TPMS/433 weather/AMR meters/garage remotes). Builds on/after P5.
**TPMS follow-on features (pinned 2026-11-16):**
- **★ Police vehicle coverage (top priority)** — confirm Ford Police Interceptor Utility, Chevy Tahoe PPV,
  Dodge Charger Pursuit (+ Durango Pursuit, F-150 Police Responder) are decoded + bench-verified. Police TPMS
  is identical to the civilian model (same OEM sensors/protocol). Also cover the generic protocols the
  programmable aftermarket sensors (Schrader EZ-sensor, Autel MX-Sensor, Huf IntelliSens) mimic.
- **Omit my own vehicle** — "learn my car" + manual entry so your own 4 sensor IDs never flag as followers.
- **★ Known-police-vehicle ID database / watch list** — sensor IDs are fixed and unique (like MACs), so
  tagging a confirmed police vehicle gives a high-confidence alert when it passes again; make/model alone
  stays a low-confidence "Fleet-type vehicle" tag. Local watch list first; **shared/downloadable database
  in scope** (WiGLE/DeFlock precedent). Match on protocol + ID; entries expire + need multi-user
  confirmation (sensors change with tire/wheel swaps); ID-only DB hosted as repo JSON. **Fusion alert:**
  fleet-model TPMS hit + Axon BLE (OUI 00:25:DF) / fleet-router Wi-Fi at the same time = strong signal,
  works on unmarked cars.
- **Downloadable TPMS decoder packs** — decoder definitions as downloadable files (like SubGHz packs);
  feasibility study owed.
Firmware-side: BLE + WiFi AP spam · deauth detection · POCSAG/FLEX pager decode ·
**nRF24 raw ESB sniffer/logger** (with legacy-keyboard keystroke decode) ·
**nRF24 ESB capture→replay + fuzzing** (own device). *(Reviewed 2026-11-05: promiscuous sniffing,
2.4 GHz spectrum scanner, and the jammer are already in the firmware — MouseJack/NrfSpectrum/NrfJammer;
BLE-adv spoofing via nRF24 was ruled out in favour of the ESP32-S3's real BLE radio.)*
App-side: **Chameleon Ultra control over Bluetooth** (RFID/NFC slots, read/emulate, alongside the Crow; reimplemented from the published protocol docs, not copied code) · **persistence-over-time tracker detection** (anti-stalking: flag a Find My/Tile/SmartTag
address that stays with you across time + movement; rides on P5 background scanning + a location log;
MAC rotation ~15 min caps cross-journey tracking). **Scoped to item-tracker categories ONLY — surveillance /
LE / drone detection (Flock, Axon/Taser body cams + stun guns, ALPR, ShotSpotter, cameras, Ring, fleet,
drone) is the priority and keeps its immediate single-hit flagging, never gated behind persistence logic.** · dedicated drone-monitor view · OTA Update Hub ·
web-flasher "Request a feature" dialog ·
first-responder dispatch feed (experiment) · app-wide theme rollout · CI enablement ·
publish `signatures.json` live on `main` (optional auto-refresh Action).
