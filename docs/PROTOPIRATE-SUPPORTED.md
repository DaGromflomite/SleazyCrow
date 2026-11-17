# ProtoPirate — supported key fobs

Plain list of what the car key-fob tool reads and what it can build a next code for.
For testing on fobs and vehicles you own or are allowed to test.

## What the two columns mean

- **Capture + Read** — the Crow picks up the fob's signal and shows you what it is
  (make, button, rolling counter where the protocol exposes one).
- **Next code** — on top of reading, it can work out the *next* valid rolling code and
  send it. For the encrypted makes this needs the correct maker key loaded first; for
  the others it works straight from one capture.

## Supported now

| Brand / system | Capture + Read | Next code |
|---|---|---|
| VW · Audi · Seat · Škoda (VAG) | Yes | Yes — needs the maker key |
| Ford | Yes | Yes |
| Fiat | Yes | Yes |
| Hyundai · Kia (V3/V4) | Yes | Yes — needs the maker key |
| StarLine (aftermarket alarm) | Yes | Yes — needs the maker key |
| Chrysler · Dodge · Jeep · RAM (FCA) | Yes | Not yet |
| Peugeot · Citroën (PSA) | Yes | Not yet |
| Kia (V0 / V1 / V2 / V5 / V6) | Yes | Not yet |
| Subaru | Yes | Not yet |
| Suzuki | Yes | Not yet |
| Scher-Khan (aftermarket alarm) | Yes | Not yet |

"Not yet" means the protocol doesn't hand us a clean rolling counter to roll forward,
so there is no next-code feature for it. That is a limit of the protocol, not a bug.

## Which keys ship in the firmware

- **Built in and working out of the box: VAG Type-2 (XTEA).** This key is public, so it's
  compiled into the firmware. It covers **VW, Audi, Seat and Škoda** key fobs that use the
  VAG Type-2 (XTEA) rolling code — read and next-code both work with no setup.
- **Everything else needs a key you load.** The other next-code makes — StarLine, Fiat,
  Ford, Hyundai/Kia (V3/V4), and the VAG AUT64 types (1, 3, 4) — use KeeLoq or AUT64
  manufacturer keys. Those keys are **not published in the clear anywhere** (every upstream
  — RocketGod's ProtoPirate, the Flipper firmwares — keeps them in an encrypted keystore on
  purpose), so none ship by default. You add the key for a make you're authorized to test,
  and then that make's next-code works too.
- **Reading always works without a key.** Capturing a fob and showing the make, button and
  counter needs no key at all — that works for every supported make right now.

## Loading your own keys

Two ways, and they stack:
- **On the SD card** — put them in `/DATA/KEYS/keeloq_mfcodes.txt` (format in
  [`examples/keeloq_mfcodes.example.txt`](examples/keeloq_mfcodes.example.txt)).
- **Compiled into the firmware** — paste them into `include/pp_embedded_keys.h` and rebuild,
  so they ship with the firmware and load at boot.

Only load keys for fobs and vehicles you own or are allowed to test.

## Notes

- **Bench-checked, not car-checked.** Every "Yes" above is proven on the device with a
  capture → next-code → re-read test that lands on the right counter. It has not been
  fired at a real vehicle. That last step needs a fob and car you're allowed to test.
- **Adding a make later.** New decoders ship with firmware updates (over Bluetooth).
  Rolling-code makes can't be added as a downloadable file — the math has to be built
  into the firmware — so a new make means a new firmware release.

## Status by make, for the record

The on-device self-test proves the next-code makes end to end
(`fwd self-test`, ProtoPirate). VAG Type 3/4 was the last one fixed and is now
verified on hardware (4/4 of VW/Audi/Seat/Škoda). See
[`PROTOPIRATE-FORWARD-CODES.md`](PROTOPIRATE-FORWARD-CODES.md) for the detail.
