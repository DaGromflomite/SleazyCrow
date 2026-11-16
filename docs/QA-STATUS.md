# Sleazy Crow — QA RESULTS (field-test readiness)

## VERDICT: ready for field test. Core (TPMS follower detection) rock-solid.

### PASS — verified on real hardware
- **TPMS decode, 9 protocols** (Ford, Toyota, Hyundai/Kia, Schrader, EG53/GM, Honda,
  TRW, Jeep/FCA, Nissan) — each a 4-wheel car over RF (Flipper→Crow), exact
  ID/pressure/temp, ZERO ghosts, ZERO cross-protocol false reads. Anchored demod
  (commits 73a0219→20fbabc) fixed a real deterministic phantom-sensor bug.
- **APK path end-to-end** — rebuilt APK shows correct make labels (Jeep/FCA etc.),
  pressures, temps, FOLLOWER flags on the Pixel. Scrollbar added for long lists.
- **CC1101 self-test** T1 liveness PASS (both radios), T2 timing PASS (+0.05%),
  T3 TX→RX keying PASS all bands (315/433/868) — covers the sub-GHz jammer radiating.
- **Sub-GHz TX fidelity** — Flipper independently decoded the Crow's Princeton tx
  (Key 0x00123456, 350us) exactly.
- **File management** CRUD over BLE (mkdir/upload/list/copy/rename/delete) — verified.
- **nRF24** present (SPI), responds to init/spectrum/scan over BLE.
- **nRF jammer — CRITICAL BUG FOUND & FIXED (2026-11-16).** The turbo jam loop
  ran at prio 2 on core 1 and only called taskYIELD(), starving the lower-prio
  BLE command task on the same core. Result: once started, the jammer could NOT
  be stopped over BLE, and because it holds the shared HSPI mutex it WEDGED both
  the nRF and the CC1101 radios (incl. TPMS) until a physical power-cycle.
  HackRF + BLE verified: during jam getState got 0/8 responses; stop got no ack;
  nRF present=NO afterward. FIX: vTaskDelay(1) every 48 turbo iters so the
  command task runs. Re-verified: getState 8/8 during jam, stop acks + running=0,
  nRF recovers, 6/6 arm/stop cycles clean, and CC1101/TPMS RX restarts after a
  jam cycle. ~85% jam duty retained.
- **Device** getState / settings-sync / OTA-status / version respond; signal scanner
  runs + reports.

### Gaps needing specific hardware (NOT firmware faults)
- nRF24 **jammer 2.4GHz emission** — HackRF host tools NOW installed (PothosSDR,
  hackrf_sweep). Jammer control path fully fixed/verified (see above). Direct
  emission witness still INCONCLUSIVE: guard-band 2484-2505 showed no clear rise
  (may be outside FULL mode's channel set); an early WiFi-band "+20dB" reading was
  a FALSE POSITIVE from ambient WiFi. TODO: sweep the exact nRF channel set the
  active mode uses, and raise PA, to confirm radiated power.
- **MouseJack injection** — needs a real vulnerable Logitech/MS 2.4GHz dongle (none).
- **ProtoPirate** decode — needs a captured car key-fob signal to replay.
- T5 PA-power delta INCONCLUSIVE — two radios too close, RSSI saturated (rig limit).

### Deferred decoders (worldwide pack, pinned) + next wave
- GM-Continental (MC_ZEROBIT), Toyota PMV-107J (differential Manchester) — need demod
  variants. BMW/Mercedes/Kia byte-aligned, addable like Jeep. See memory pins.

### PRE-DRIVE: clear the Pixel's TPMS history (⋮ -> Clear history) so the lab demo
### sensors (Jeep 0x8A11C40x) don't flag as followers. Keep band on your target MHz.

---
# Live QA status (resume point)

Updated continuously during the unattended QA run. If a session resumes, READ THIS
to continue. Bench rig + methods are in `docs/LAB-TEST-PLAN.md`.

## Hardware (ports)
Crow: COM15 (flash) + BLE F0:24:F9:84:05:BE · Flipper "GromFlip" Momentum: auto-detect
VID 0483:5740 (COM10) · HackRF/PortaPack: COM20 · Pixel: adb 3B020DLJG0002S.
Flash: `C:/Users/Timot/AppData/Local/Programs/Python/Python312/Scripts/pio.exe run -e esp32dev -t upload --upload-port COM15`.
Test harness: scratchpad `multi_tpms.py` (TPMS per-proto), `rf_lib.py`, `playlist_air.py`,
`nrf_test.py`, `fw_exact.py` (faithful decode model). Alerts: `python alert.py "T" "M"`.
Pixel BLE toggle to clear GATT cache: `adb shell cmd bluetooth_manager disable/enable`.
Only ONE BLE client at a time (Crow=1 conn): Pixel BT OFF while PC holds BLE.

## DONE — hardware verified (live RF)
TPMS decode, anchored demod (commits 73a0219→ad8a123, Jeep/Honda ade6f8f, Nissan 20fbabc):
9 protocols, each a 4-wheel car via Flipper→Crow, exact ID/pressure/temp, ZERO ghosts:
Ford(1) Toyota(2) Hyundai-Kia(3) Schrader(4) EG53-GM(5) Honda(6,marker) TRW(7) Jeep(8) Nissan(9,preamble).
Sub-GHz TX fidelity: Flipper decoded Crow's Princeton transmit exactly (Key 0x00123456).
nRF: presence + RX spectrum tracks real 2.4GHz (nrf_test.py).
APP display: Ford 4-wheel shown correctly on Pixel (earlier). Other protos: pending app-path.

## Progress this run
- 9 TPMS protocols hardware-verified (Ford,Toyota,Hyundai,Schrader,EG53,Honda,TRW,Jeep,Nissan). commits to 20fbabc.
- Files CRUD verified over BLE (mkdir/upload/list/copy/rename/delete). getState ok.
- Signal scanner: runs + reports detections to Flipper TX.
- APK-PATH FINDING: installed APK was OLD -> showed Jeep as "Unidentified" though firmware
  decoded correct pressure/temp. REBUILDING APK (new proto 6-9 labels + TPMS scrollbar).
  After install, re-verify labels show Honda/TRW/Jeep/Nissan.

## TODO (this run) — every function, verified, APK path included
- [~] APP-path TPMS: rebuilding APK with proto labels; then show Jeep/Honda/Nissan named on screen.
- [x] App scrollbar for long sensor lists (added, in this APK build).
- [ ] Sub-GHz TX: transmit-from-file (done), playlist order/gap/repeat, transmit-binary, brute/deBruijn (Flipper rx), jammer (self-test T3 covers TX radiate).
- [ ] Sub-GHz RX: record→.sub verify, signal scanner (done-runs), frequency search, ProtoPirate (needs car-fob signal).
- [ ] nRF: jammer ON-AIR via HackRF 2.4GHz witness; MouseJack scan arm (status re-probe); attacks arm.
- [ ] Files: list/mkdir/upload/download/rename/copy/move/delete/tree over BLE.
- [ ] Device: getState, version, set name, set time, settings sync, CC1101 power; reboot; SDR (CC1101 spectrum/raw); OTA version+MD5 read (no random flash); HW button config. Factory-reset/format-SD: verify guarded, DO NOT wipe.
- [ ] CC1101 self-test T1–T7: flash env:esp32dev-selftest, capture serial, reflash esp32dev.
- [ ] Final: clear Pixel TPMS history, leave release firmware flashed, summary.

## Open hardware gaps
- MouseJack injection needs a real vulnerable Logitech/MS 2.4GHz dongle (none present). HackRF can see carrier, not act as victim.
- Honda/Nissan use dedicated marker/preamble paths; GM-Continental/PMV-107J (differential MC) deferred to the worldwide hosted-pack engine (pinned).

## Alerts
ntfy channel reaches the user's iPhone (gfhb-drones-...). On a HARDWARE BLOCKER
(wedged port needing replug, missing device): run alert.py AND stop with a clear
terminal banner. Email timothyhrandall@proton.me requested but needs interactive
auth not available unattended — ntfy + terminal banner is the channel.
