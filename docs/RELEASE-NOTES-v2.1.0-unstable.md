# Sleazy Crow v2.1.0-unstable — what's new, what works, what's untested

> ⚠️ **UNSTABLE TEST BUILD.** Use at your own risk. Most of what's below was tested on real hardware this
> round, but it has **not** been tried near a real moving car in traffic. If something misbehaves, go back to
> the stable **v1.2.0-beta** with the web flasher (main page) — it always stays available.

Firmware **2.1.0-unstable** · App **2.1.0-unstable** (build 41) · Built from branch `v2-unstable`.
Everything here is on top of **v2.0.0-unstable**.

**How to read this**

| Mark | Meaning |
|---|---|
| ✅ **Works** | Tested on a real Crow with real radio signals (Flipper / HackRF / real fob) |
| 🧪 **Untested near a car** | Works on the bench, but not yet tried in real traffic |
| ⚠️ **Known issue** | Something we know is still wrong or incomplete |

---

## 1. "Is a car following me?" detector (TPMS)

- ✅ **Reads 9 tire-sensor types now**, covering most common US makes: Ford, Toyota/Subaru, Hyundai/Kia,
  GM/Chevy, Honda, Nissan/Infiniti, Jeep/Dodge/RAM, plus Schrader and TRW. Each one was tested on the bench
  by sending the real sensor signal from a Flipper and watching the Crow and app read the right tire, pressure,
  and temperature — with zero false reads and no mix-ups between makes.
- ✅ **No more fake "phantom" sensors.** A weak-checksum bug let bad bit alignments pass as real cars, which
  could show a car that wasn't there. The decoder now locks onto the start of the signal, so it only reports
  sensors that are really there (0 fakes across 16,000 test signals, then confirmed on hardware).
- ✅ **App:** the sensor list now has a scroll bar, and each make shows its name.
- 🧪 **Not yet tested near a real car on the road.** The bench signals are the same data a car sends, but
  real-world range, traffic, and sensors that only transmit while driving are still untested.

## 2. Jammer safety fix (important)

- ✅ **The 2.4 GHz jammer no longer gets stuck ON.** Before, starting it could freeze both radios — including
  the car detector — until you unplugged the Crow. It now stops cleanly every time (confirmed with a HackRF
  watching the air, over 6 start/stop cycles, and the car detector still works right after).

## 3. Car key-fob reader (ProtoPirate)

- ✅ **Now reads Chrysler / Dodge / Jeep / RAM fobs.** This make was never supported before. It was
  reverse-engineered from a real 2019 RAM fob and confirmed live on the device (decodes to a steady serial).
- ✅ **A signal it can't identify is now saved and shown as "Unknown"** instead of silently vanishing, so you
  can tell it actually heard something. The raw capture is written to the SD card for later.
- ⚠️ **"Load .sub" (reading a saved file back through the decoders) is still being fixed.** One real bug was
  fixed (it was throwing away half of every signal), but loading a file offline still doesn't always produce a
  result. The **live** path (press the fob, read it on the air) works.

## 4. Transmitting

- ✅ **"Transmit Binary" works.** You can send a raw on/off pattern on a frequency you pick. The button used
  to do nothing. Confirmed on a HackRF (it decoded the exact pattern the Crow sent).

## 5. Reliability

- ✅ **Flashing the firmware is reliable now.** The upload speed was too fast and sometimes failed partway;
  it's now a steady speed that works every time.

---

## Still untested / needs special hardware (honest list)

- 🧪 Everything in the car detector: untested near a real moving car.
- ⚠️ The jammer's actual 2.4 GHz output strength wasn't independently measured (the control/on-off works; the
  radiated power couldn't be witnessed with the gear on hand).
- ⚠️ MouseJack keystroke injection needs a real vulnerable 2.4 GHz dongle to confirm.
- ⚠️ Some sub-GHz SDR and a few device-settings commands weren't exercised this round.

As always: go back to **v1.2.0-beta** if anything here misbehaves.
