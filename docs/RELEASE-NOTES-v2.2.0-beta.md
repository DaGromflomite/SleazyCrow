# Sleazy Crow v2.2.0-beta — what's new and what's still being proven

**This is a beta.** The reliability bugs that made the earlier builds rough are
fixed and tested on real hardware, so it's past the "unstable" stage. It is not
final, and two of the headline features still need to be proven in the real
world (see the bottom of this page). If anything acts up, you can go back to
**v1.2.0-beta** from the main web flasher at any time.

Firmware **2.2.0-beta** · App **2.2.0-beta** · Built from branch `v2-unstable`.

---

## New in this build

### Key-fob next code (for authorized testing only)

The key-fob reader can now build the **next** rolling code for a fob, not just
read it. It reads a press, moves the counter forward, re-encrypts it with a
manufacturer key you load, and sends it — the same thing the real fob does on the
next press.

This is for testing on vehicles and fobs **you own or are allowed to test.** It
needs the manufacturer key loaded first. Capturing a signal does not hand you the
key, and the tool will not guess it, so it can't make codes for a random car.

Working and checked on **StarLine** and **Fiat** so far. A few other makes are
read but can't be rolled forward yet (listed below). There's a full guide,
including how to load keys, in `docs/PROTOPIRATE-FORWARD-CODES.md`.

### Car-follower screen

The "is a car following me?" screen got a **Clear** button and a **scroll bar**,
so a long list of tracked vehicles is easy to wipe and scroll through.

---

## Already in the v2 line (recap)

- The "is a car following me?" detector reads 9 tire-sensor types, covering most
  common US makes.
- Key-fob reader handles Chrysler, Dodge, Jeep, and RAM (checked against a real
  2019 RAM fob).
- Sub-GHz capture, replay, brute force, playlists, and downloads from the Flipper
  database and UberGuidoZ's library, with a Play button right on the Sub-GHz
  screen.
- "Transmit Binary" sends a raw on/off pattern on a frequency you pick.
- 2.4 GHz jammer and nRF tools.

---

## Fixed and tested on real hardware

- The 2.4 GHz jammer no longer gets stuck on and freezes the radios. Checked over
  six start/stop cycles with a HackRF watching the air.
- Bluetooth no longer drops a few seconds after connecting.
- Flashing the firmware is reliable instead of failing partway.
- Long transmits no longer trip the watchdog and reboot the Crow.
- Downloads to the SD card are faster and won't save a corrupted file.
- The car-follower detector stopped showing fake "phantom" sensors.

---

## Still being proven (why it's beta, not final)

- **The car-follower detector hasn't been tried next to a real car on a drive.**
  The bench tests use the same signals a car sends, but real range, traffic, and
  sensors that only talk while the car is moving are untested.
- **The key-fob next-code feature hasn't been fired at a real vehicle.** The math
  is checked on the device and in testing, and it works on StarLine and Fiat, but
  it hasn't opened a real (owned/authorized) car yet.
- Ford, VAG, and KIA fobs are read but can't be rolled forward yet — each needs
  its own encoder written and checked against a real fob of that make first.
- The offline "Load .sub" path in the key-fob reader is still being fixed.
- The jammer's actual output strength wasn't measured, and the mouse/keyboard
  injection test needs a real vulnerable 2.4 GHz dongle.

Go back to **v1.2.0-beta** if anything here gives you trouble.
