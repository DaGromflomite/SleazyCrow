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

## Scope decisions (recorded)

- **Removed Meshtastic / LoRa detection** — out of scope; drone detection is the only mesh/RF-beacon target kept.
- **Signature sourcing stays MIT-clean** — re-derived from primary public sources, GPL lists used only as
  pointers.
- **Playlists are phone-side** (not an SD-only format) so they work offline; SD export is opt-in.

---

## Pinned for later (not yet in v2)

Roadmap: **P4** WiFi scanner (lights up the staged ALPR/Ring/camera/body-cam/hacker signatures) ·
**P5** passive background alerts (opt-in) · **P6** Sub-GHz fox hunt *(firmware)* · **P7** polish + release.
Firmware-side: BLE + WiFi AP spam · deauth detection · POCSAG/FLEX pager decode ·
**nRF24 raw ESB sniffer/logger** (with legacy-keyboard keystroke decode) ·
**nRF24 ESB capture→replay + fuzzing** (own device). *(Reviewed 2026-11-05: promiscuous sniffing,
2.4 GHz spectrum scanner, and the jammer are already in the firmware — MouseJack/NrfSpectrum/NrfJammer;
BLE-adv spoofing via nRF24 was ruled out in favour of the ESP32-S3's real BLE radio.)*
App-side: dedicated drone-monitor view · OTA Update Hub · web-flasher "Request a feature" dialog ·
first-responder dispatch feed (experiment) · app-wide theme rollout · CI enablement ·
publish `signatures.json` live on `main` (optional auto-refresh Action).
