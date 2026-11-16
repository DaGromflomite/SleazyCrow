# Sleazy Crow — Lab Test & Debug Plan (pre-road-test)

Goal: verify **every** firmware function on real hardware in the lab, simulating all
RF with the bench rig, so the road test confirms rather than discovers. No guessing —
every item has a real TX/RX witness.

## Bench rig
| Device | Role | Port |
|---|---|---|
| Sleazy Crow (ESP32, 2× CC1101 + nRF24L01) | **DUT** | COM15 (flash) + BLE (F0:24:F9:84:05:BE) |
| Flipper Zero "GromFlip" (Momentum) | sub-GHz TX/RX witness, 300–928 MHz | COM10 (auto-detect VID 0483:5740) |
| HackRF + PortaPack/Mayhem | 2.4 GHz + wideband witness (nRF TX, jam) | COM20 |
| Pixel (SleazyCrow app) | app display / BLE client | adb 3B020DLJG0002S |
| PC | BLE host, orchestrator | — |

Only ONE BLE client at a time (Crow = 1 connection). Swap clients by disabling the
other's Bluetooth. Known gotcha: a stale Android GATT cache → toggle Pixel BT
(`adb shell cmd bluetooth_manager disable/enable`) then reconnect.

Scripts in session scratchpad: `multi_tpms.py` (TPMS per-protocol), `playlist_air.py`
(TX fidelity), `nrf_test.py`, `rf_lib.py`, `faithful_test.py`/`verify_fix.py` (offline
decoder analysis), `tpms_ota.py`/`tpms_selfloop.py`.

---

## Part A — TPMS decoder coverage

### Shipped (5) + generic fingerprint (proto 0)
Ford/Continental (1), Toyota/Pacific PMV-C210 (2), Hyundai-Kia/VDO (3), Schrader
generic (4), Schrader-EG53/GM (5). Proto 0 = trackable 32-bit fingerprint for anything
demodulable but unrecognized (enough for follower *tracking* without make/pressure).

### US-common decoders to ADD now (port from rtl_433 public layouts + lab-test each)
| New | rtl_433 file | US relevance |
|---|---|---|
| Honda | tpms_honda.c | Honda/Acura — very common |
| Nissan | tpms_nissan.c | Nissan/Infiniti — very common |
| TRW | tpms_trw.c | cross-make (GM, Honda, FCA) — high value |
| GM-Continental | tpms_gm.c | GM (distinct from EG53) |
| Toyota PMV-107J | tpms_pmv107j.c | newer Toyota/Lexus |
| Jeep/FCA | tpms_jeep.c | Jeep/Chrysler/Dodge/RAM |
| Kia | tpms_kia.c | if distinct from hyundai_vdo |
| BMW | tpms_bmw.c / tpms_bmw_g3.c | common US luxury |
| Mercedes | tpms_mercedes_benz.c | common US luxury |
| Hyundai Elantra | tpms_elantra2012.c | specific but common |

### Feasibility — all 33 via downloadable packs (pinned, study owed)
rtl_433 has 33 `tpms_*.c`. Remaining ~18 (Renault, Citroën, Abarth, Jansite×4,
Porsche, AVE, airpuxem, iMars, TyreGuard, eezRV, gear_hive, SmartIRE, truck,
Schrader-motorcycle, sefis_m3, renault_0435r) are EU/niche/aftermarket.
Rather than compile all into firmware, **host decoder definitions for download** like
the SubGHz `.sub` catalog; users pick makes/regions. Feasibility questions:
data-driven decoder table (offsets + checksum-type + field map) vs C code; a checksum
mini-spec (add-sum / CRC-8 poly+init / XOR / none); demod support per coding
(FSK/OOK, Manchester 1=10 vs differential); pack format on SD + app browse/download
(reuse SubGHz downloader UX). **Risk:** every weak-checksum decoder can hit the
deterministic shift-ghost (fixed in 73a0219 by taking the latest valid bit-offset) —
so every pack/decoder MUST be synthetic-frame hardware-tested before release.

### Per-decoder test method (the "Ford procedure", repeat for each)
1. Read the rtl_433 `.c`: preamble/sync, byte count, checksum (type+range), ID/
   pressure/temp field maps, modulation (FSK/OOK), bit coding.
2. Add `decodeX()` to `TpmsDecoders.h`; wire into `decodeAny()` under the right family.
3. Offline: synth a faithful frame (real preamble), run `faithful_test`-style all-offset
   scan — confirm the true ID is picked and **no shift-ghost** survives `packAndDecode`
   (latest-offset). Add a noise pass.
4. Build + flash. Over-the-air: Flipper transmits a synthetic multi-sensor "car" of that
   make; Crow must decode exact ID/pressure/temp, no phantom IDs (`multi_tpms.py`).
5. App display: Pixel on TPMS tab shows correct ID, make label, psi, °C, FOLLOWER.
6. Regression: re-run Ford + Schrader to confirm no cross-protocol breakage.

**Status:** Ford ✅ (bug found+fixed 73a0219; 4-wheel decode + Pixel display verified).
Others: pending.

---

## Part B — Full firmware function matrix (every app feature)

Legend: ⬜ pending · ✅ pass · ⚠ issue · 🔧 fixed

### Sub-GHz
- ⬜ Transmit from file — Flipper decodes Crow air (Princeton/CAME), bit-exact.
- ⬜ Playlist (order/gaps/repeat/loop) — Flipper decodes sequence A,A,B,B. *(TX path
  confirmed: Crow accepted+sent all 4; receiver syntax fixed to `subghz rx <hz> 0`.)*
- ⬜ Transmit binary (freq/pulse/data) — Flipper decodes.
- ⬜ Record — Flipper TX a known code; Crow captures to .sub; pull + verify bits.
- ⬜ Signal scanner (RSSI) — Flipper TX; Crow shows freq/RSSI.
- ⬜ Frequency search — Flipper TX on a freq; Crow finds it.
- ⬜ ProtoPirate decode — Flipper TX known protocol; Crow decodes proto+key; .sub import.
- ⬜ Brute / de Bruijn — Flipper RX; codes stream out.
- ⬜ Sub-GHz jammer — Flipper RX sees carrier/noise; stops cleanly.

### TPMS
- ✅ Ford 4-wheel decode + Pixel display + follower flag (433.92). Fix 73a0219.
- ⬜ Each added US decoder (Part A procedure).
- ⬜ 315 MHz band (RX proven earlier; re-confirm with region-unlocked Flipper).
- ⬜ Mixed-make scene (several protocols at once) — no cross-talk/ghosts.

### nRF24
- ✅ Presence (SPI), ✅ RX spectrum tracks real 2.4 GHz.
- ⬜ Jammer radiates — **HackRF witness** on 2.4 GHz.
- ⬜ Spectrum vs HackRF carrier at a known channel (calibrate).
- ⬜ MouseJack scan arms+sweeps (re-probe raw frames). Inject = needs real dongle (gap).

### Files / storage
- ⬜ list / navigate / mkdir / upload / download / rename / copy / move / delete /
  directory tree / multi-select — all over BLE, verify on SD.
- ⬜ File viewer renders .sub.

### Device / settings
- ⬜ getState, version info, set device name, set time, settings sync, CC1101 power cfg.
- ⬜ Reboot (graceful). Factory reset / SD format — verify guarded (don't wipe unless safe).
- ⬜ OTA: version read + MD5 + reboot flow (no random flash).
- ⬜ SDR (CC1101-based: spectrum + raw RX stream + HackRF-serial iface) — exercise.
- ⬜ HW button config (configure over BLE; physical press = minor gap).

### Self-test (designed release gate) — do LAST, reflash release after
- ⬜ T1 radio liveness · T2 pulse timing · T3 TX→RX keying · T4 freq peak · T5 PA power ·
  T6 environment · T7 file→antenna fidelity. Flash `esp32dev-selftest`, capture serial,
  reflash `esp32dev`.

### Open hardware gaps
- MouseJack **injection** needs a real vulnerable Logitech/MS 2.4 GHz dongle (BT HID is
  not a valid target). HackRF can see the carrier, not act as victim.

---

## Handoff before road test
Clear the Pixel's TPMS history (remove the lab demo sensors), leave app on TPMS/315,
confirm release firmware flashed. Everything committed locally; nothing pushed.
