# Sleazy Crow v2.0.0-unstable — what's new, what works, what's untested

> ⚠️ **UNSTABLE TEST BUILD.** Use at your own risk. Some features below have **not** been tested on real
> hardware yet. If something misbehaves, go back to the stable **v1.2.0-beta** with the web flasher
> (main page) — it always stays available.

Firmware **2.0.0-unstable** · App **2.0.0-unstable** (build 40) · Built from branch `v2-unstable`.
Everything here is on top of the stable **v1.2.0-beta**.

**How to read this**

| Mark | Meaning |
|---|---|
| ✅ **Works** | Tested on a real Crow and phone |
| 🧪 **Untested on hardware** | Built, and passes automated tests, but not yet tried on real hardware or in the field |
| ⚠️ **Known issue** | Something we know is wrong or incomplete |
| ➖ **Removed** | Was in earlier v2 test builds, taken out |

---

## 1. Installing and updating

- ✅ **Web flasher** — flash the firmware from the browser, plus new step-by-step "install the app from a
  computer" instructions (USB file copy or `adb install`).
- 🧪 **Unstable channel (new)** — unstable builds are only offered to people who opt in:
  **Settings → Updates → "Offer unstable updates"** (off by default, shows a warning first). Every unstable
  update is labelled **UNSTABLE** and asks you to confirm before it downloads or flashes. Phones on the old
  v1.2.0-beta app never see unstable builds.
- 🧪 **App updates itself (fixed)** — the "Download & Install" button never worked on modern Android. It now
  downloads the new app safely inside Sleazy Crow, checks the file's checksum, walks you through the one-time
  "Install unknown apps" permission, and opens Android's installer (you confirm). Only official Sleazy Crow
  releases are accepted (they must be signed with the Sleazy Crow key).
- 🧪 **Automatic update checks** — the app looks for a newer app version on launch (at most every 12 hours,
  silent unless there is one), and the Firmware Update screen checks for new Crow firmware as soon as it opens.
- ✅ **Updates come only from the Sleazy Crow GitHub** (`DaGromflomite/SleazyCrow`), never the upstream
  project — checked in code and by automated tests against the real release lists of both projects.
- ✅ **Firmware update over Bluetooth (OTA)** — unchanged from v1.2.0-beta; the firmware side was part of the
  hardware command test sweep.
- ⚠️ **Coming from v1.2.0-beta:** flash this build with the web flasher and install the APK by hand once.
  Over-the-air updates work from v2 onward.

## 2. TPMS "is a car following me?" detector (new)

The Crow listens for the tyre-pressure sensors (TPMS) every car broadcasts. Each sensor has a fixed ID, so if the
same car's sensors keep showing up around you, Sleazy Crow flags it.

- ✅ **TPMS receiver on one radio** — runs on one CC1101, leaving the other radio completely free for
  transmit / record / ProtoPirate / brute force at the same time (confirmed on the Crow).
- ✅ **Five sensor decoders** — Ford, Toyota, Hyundai/VDO, Schrader and Schrader-GM (covers GM / Chevy, Opel,
  Saab and others). Every decoder passes the Crow's built-in self-test on real hardware.
- ✅ **Both TPMS signal types** — the Crow alternates FSK and OOK every 4 seconds (many GM/Chrysler sensors use
  OOK, which a FSK-only receiver would miss); confirmed running with no crashes or reboots.
- ✅ **App screen** — each sensor shows its make (by protocol), tyre pressure and temperature; a
  **sensitivity** setting: *Immediate* (default, flags on first sighting), seen twice in 30 min / 1 h / 2 h,
  or across 3 trips. Sightings are remembered for 6 hours.
- 🧪 **Picking up a real car** — NOT tested yet. Everything was tested with the self-test and simulated
  signals; it has not yet been driven near real traffic. Receiver settings follow published TPMS specs, but
  real-world reception is unproven.
- ⚠️ Very fast FSK sensors sit close to the Crow's shortest-pulse limit and may be missed.

## 3. Sub-GHz transmit playlists (new)

- ✅ **Playlists** — a new Playlist tab in Sub-GHz: build an ordered list of `.sub` files and pauses
  (250 ms – 10 s), with per-file repeat, a gap between files and loop. Drag to reorder, rename, save.
- ✅ **Built and saved on the phone**, so you can make playlists with no Crow connected; optional
  **Export to SD** (`/PLAYLISTS`) for backup.
- ✅ **Import UberGuidoZ's ready-made playlists** — pick any of his 30 playlists; only the files it needs are
  downloaded.
- ✅ **Reports files that failed to play** at the end instead of skipping them silently.
- 🧪 **Add files from the SD card with no Crow connected** — uses the phone's saved list of the card's files
  (kept up to date automatically). Covered by automated tests, not yet tried in the field.
- ⚠️ **The Crow can disconnect during long playlists.** A keep-alive / auto-resume is still to do.

## 4. Sub-GHz, ProtoPirate and transmitting

- ✅ **Play Sub File** button on the Record screen — pick any `.sub` on the card and transmit it directly.
- ✅ **ProtoPirate "Load .sub" now browses folders** (it used to list only ~50 files, so downloaded
  collections were invisible).
- ✅ **ProtoPirate file-load crashes fixed** (firmware) — large or complex files no longer crash the Crow;
  tested with a real Isuzu fob file (decoded correctly) and a large Tesla file that used to crash.
- ✅ **Long transmits no longer reboot the Crow** (firmware) — a 13-second transmit used to trip the
  watchdog, reboot the Crow and drop Bluetooth. Fixed and tested: recorded a live key-fob press on one radio
  while transmitting the long file on the other, with no reboot.
- ✅ **ProtoPirate file browser no longer freezes** waiting for a listing.
- ℹ️ The jammer runs until you press **Stop Jam** (its timer is a cool-down, by design).

## 5. SD card, files and downloads

- ✅ **.sub downloader now uses UberGuidoZ's collection** — browse his Sub-GHz folders and download only
  what you pick (his folder layout is kept on the card).
- ✅ **Big files no longer get silently cut off** — uploads over ~122 KB were truncated by the Crow (an
  upstream bug). The app now refuses uploads it can't deliver whole and splits large RAW files into
  ~100 KB pieces; checked losslessly on real files up to 3.2 MB.
- ✅ **Fast SD format** (firmware) — rebuilds the card in one pass instead of deleting file by file
  (10+ minutes); works in normal use. Falls back to the slow method if needed.
- ✅ **Format waits for the Crow to finish** — both format buttons now wait for the Crow's "done" message, so
  downloads can't start on a half-wiped card.
- ✅ **Deleting deep folders fixed** (firmware) — very nested folders used to fail to delete.
- ✅ **Delete captured signals** — Record list long-press now has Delete.

## 6. General app changes

- ✅ **No Crow? Still browse everything** — every tab opens without a Crow; only actions that need the
  hardware ask you to connect.
- ✅ **Keep screen awake** (Settings → Connection, on by default) — stops the screen sleeping and dropping
  Bluetooth mid-transmit. OTA and downloads no longer switch it off afterwards.
- ✅ **Clearer connection errors** — a timeout no longer shows up as "Permission Error", and says what to
  check. Connect timeout raised from 10 s to 15 s.
- ✅ **Connecting is more reliable** — the app stops other scans before connecting.
- ✅ **Look and feel** — pink→purple Sleazy Crow styling and a faint logo instead of empty black screens.

## 7. Removed since earlier v2 test builds

- ➖ **Phone-based surveillance/tracker detection** (BLE / Classic Bluetooth / WiFi scanning for Flock, Axon,
  ALPR cameras, AirTags, skimmers, drones, fox hunt). Removed: the phone's radios can't reliably see the gear
  it was meant to catch (deployed Flock cameras use LTE, not WiFi or Bluetooth).
- ➖ **Crow WiFi sniffer** — the Crow's chip doesn't have enough memory to run WiFi alongside Bluetooth.
  Removing it freed a lot of memory (free RAM 15 KB → 51 KB) and flash space.

## 8. Known issues (summary)

- ⚠️ Crow may disconnect during long playlist playback.
- 🧪 TPMS has not been tested near a real moving car yet.
- 🧪 App self-update and the unstable channel have not yet been tried end-to-end on a phone (they need a
  later release to update *to*).

## Going back to stable

Open the web flasher's main page and flash **v1.2.0-beta**, then install `SleazyCrow-v1.2.0-beta.apk`.
If the app won't install over the unstable one, uninstall Sleazy Crow first.

Checksums: every file in the release has a matching `.md5`.
