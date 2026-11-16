# Sleazy Crow — live QA status (resume point)

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
terminal banner. Email [redacted] requested but needs interactive
auth not available unattended — ntfy + terminal banner is the channel.
