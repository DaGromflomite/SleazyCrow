# ProtoPirate self-test (synthetic decode)

Proves the ProtoPirate decode pipeline works **without any real automotive
signal** — fully ethical, self-contained.

`gen_subaru_sub.py` replicates `PPSubaru::generatePulseData` (PWM: te_short=800
HIGH=1, te_long=1600 HIGH=0, 64-bit, 80x preamble, -4000 sync) to emit a valid
synthetic Subaru frame as a Flipper `.sub`. Because `PPSubaru::processData()`
sets `crcValid=true` with no crypto/CRC gate, any 64-bit value round-trips.

## Run
1. `python gen_subaru_sub.py`  -> writes `PPTest_Subaru.sub` (data=0x1A2B3C4D5E6F7788)
2. Put it on the Crow SD (Files tab -> upload, into /DATA/SIGNALS), or copy to the card.
3. ProtoPirate -> folder -> Manual path -> `/DATA/SIGNALS/PPTest_Subaru.sub` -> Analyze.

## Verified 2026-11-05 (hardware, over BLE)
Decoded: **Subaru · CRC OK**, Data `0x1A2B3C4D5E6F7788`, Serial `0x1A2B3C`,
Button 6, Counter 46213 — exact round-trip. ProtoPirate confirmed working.
