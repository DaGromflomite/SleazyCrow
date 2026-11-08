# Sleazy Crow v2 — Changelog & Feature Documentation

**Status:** in development on branch `dev` (released line `v1.2.0-beta` is frozen on `sleazycrow`/`main`).
This document is the authoritative, running record of **everything changed and every feature added**
for the v2 release, so v2 can ship fully documented. It is updated as work lands — keep it current.

Baseline: everything below is on top of the published **v1.2.0-beta** firmware + app.

---

## Headline features in v2

1. **Phone-based surveillance / tracker detection suite** (works with **no Crow attached**).
2. **The Crow is now optional** — every menu is browsable offline; only hardware functions wait for the device.
3. **Sub-GHz transmit playlists** — build, save, and play ordered lists of `.sub` files with pauses.

---

## 1. Detection suite (phone radios, no Crow required)

### P0 — Home hub + "Crow optional"
- Home screen became a **hub** with **Connect** and **Detect** sub-tabs.
- **Detect** runs on phone radios alone (no Crow needed); **Connect** keeps the original pairing flow.
- Files: `lib/screens/home_screen.dart`, `lib/screens/detect_screen.dart`.

### P1 — BLE scanner + fox hunt
- Live BLE device list (name / address / RSSI), flagged devices float to the top.
- **Fox hunt:** tap a device → gradient ring signal meter, live dBm, warmer/colder trend, haptic pulses
  that speed up as you close in. RSSI-only (phones can't do true AoA bearing).
- Files: `lib/screens/detect_screen.dart`, `lib/screens/hunt_screen.dart`,
  `lib/widgets/gradient_widgets.dart`, `lib/theme/app_colors.dart` (cyber pink→purple).

### P2 — Detection engine + signature database
- **Grading engine** (High / Med / Low) over an updatable signature DB.
- **DB delivery:** bundled `assets/detection/signatures.json` (always works offline) **plus** a background
  auto-fetch from the repo raw URL — update detections **without an app release** by editing one JSON file.
  Bundled copy is the permanent fallback; nothing to self-host or keep alive.
- **Per-category toggles** ("What to flag"), persisted; iBeacon off by default.
- **Signature list v7 — 19 categories**, all reimplemented from **primary public sources**
  (IEEE MA-L OUI registry, Bluetooth SIG assigned numbers, ASTM F3411 / OpenDroneID, Apple Find My /
  iBeacon, Google FMDN, DULT). GPL projects (SquatchWatch-CYD, surveillance-signatures) were used **only**
  as a pointer to which identifiers to verify, never copied — keeps the app MIT-clean.
  - **Fire on BLE now:** Flock Safety, Axon/Taser body cam, camera glasses (Meta Ray-Ban / Snap Spectacles),
    fleet GPS/ELD (Samsara/PeopleNet), DULT unknown-tracker, ShotSpotter/Raven, AirTag, Tile,
    Samsung SmartTag, Google Find My, Drone (OpenDroneID), Flipper Zero, card skimmer.
  - **Staged for P4 (WiFi):** license-plate readers (ALPR), body-worn cameras, Ring (13 OUIs),
    generic security cameras, hacker gear (Pineapple / Pwnagotchi / O.MG).
  - Deliberately **omitted** the weak chip-vendor OUIs (SiLabs/Liteon/Espressif) that cause false positives.
  - ShotSpotter OUI `d4:11:d6` confirmed against the IEEE registry.
- **UI polish:** the "What to flag" sheet groups categories under section headers
  (**Surveillance · Trackers · Drones · Tools · Other**) and shows a **friendly group label** on each row
  (e.g. "Surveillance", "Item tracker", "Hacker tool") instead of the raw severity keyword.
- Files: `lib/detection/detection_models.dart`, `lib/providers/detection_provider.dart`,
  `assets/detection/signatures.json`, `lib/screens/detect_screen.dart`.

### P3 — Classic Bluetooth + card-skimmer detector
- Native Kotlin channel does classic-BT inquiry; classic devices merge into the Detect list.
- Card-skimmer category flags the common cheap modules (HC-05/06, RN-42, BT04, Linvor, JDY-3…).
- Files: `android/.../MainActivity.kt`, `lib/services/classic_bt.dart`, `lib/providers/detection_provider.dart`.

### Drone detection
- BLE **OpenDroneID / Remote ID** (service `0xFFFA`) detection is **live now** in the Detect suite.
- Pinned for later: a dedicated live drone-monitor view (like the Mesh Monitor widget) + WiFi Remote ID (P4).

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

- Cyberpunk **pink→purple** gradient identity on the new Detect/Hunt/Playlist screens.
- **Logo watermark:** blank areas show a very subtle, desaturated Sleazy Crow emblem instead of a flat
  black wall — on the main Connect screen (disconnected), the Detect "no devices" screen, the playlist
  library and editor (empty, and faintly behind a short list). Reusable `LogoWatermark` / `LogoEmptyState`.
  Files: `lib/widgets/logo_watermark.dart`, `lib/screens/home_screen.dart`,
  `lib/screens/detect_screen.dart`, `lib/screens/playlist_screen.dart`.

---

## 5. Testing

First automated tests in the project (`flutter test`), all passing:
- `test/detection_signatures_test.dart` — 8 tests over the bundled signature DB + parser (version/count,
  unique ids, LoRa/mesh removed + drone kept, iBeacon off-by-default, rule well-formedness, lowercase
  storage, known BLE anchors AirTag/skimmer/drone/ShotSpotter).
- `test/sub_playlist_test.dart` — 5 tests over the playlist model (file vs pause entries, JSON round-trip,
  file counting, defaults).
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

**In progress / next (5):**
- **Crow disconnects during playlist playback** — needs a BLE keep-alive during inter-file gaps and/or
  mid-playlist auto-reconnect+resume (keep-screen-awake alone isn't enough).
- **UberGuidoZ playlist compatibility (to verify)** — keep the phone-side playlist feature; separately check
  whether his repo ships Flipper `.txt` playlists and, if so, translate their `/ext/subghz/...` paths to the
  Crow's `SUB Files/` layout + confirm the firmware can play that format.

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
