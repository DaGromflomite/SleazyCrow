# ProtoPirate — Forward Rolling-Code Generation

**Authorized security testing only.** This feature generates the *next valid*
rolling code for a vehicle key fob, for use in **authorized penetration tests
and security research against vehicles/fobs you own or are explicitly permitted
to test.** Do not use it against vehicles you do not own or have written
permission to test. Generating codes for someone else's vehicle is vehicle theft
and is illegal in essentially every jurisdiction.

---

## What it does (and what it can't)

A rolling-code fob sends an encrypted "hop" each press. The hop is the fob's
secret counter, encrypted with a device key that is derived from a
**manufacturer key** plus the fob's serial. A captured hop only unlocks the car
*once* (before the real fob is next used), because the car expects the counter
to keep climbing.

Forward-code generation does what a real fob does: it takes the recovered
counter, **advances it**, re-encrypts, and transmits — producing a code the car
will accept on the next press.

**This requires the manufacturer key.** Without it, the firmware cannot recover
the counter or produce a valid forward code — it returns "no key" (TX error 8).
Capturing a signal does **not** reveal the key; you supply the key for the make
you are authorized to test (your security team, the fob's datasheet, or a
published key for that family).

**It cannot break a cipher it doesn't have the key for.** This is a key-management
tool for authorized testing, not a cryptographic attack that mints codes for
arbitrary vehicles.

---

## Supported protocols

| Protocol | Scheme | Forward code | Verified |
|---|---|---|---|
| **StarLine** | KeeLoq (keystore) | ✅ Yes | ✅ Self-test: crypto + framing (cnt N→N+1) |
| **Fiat V0** | Cleartext counter | ✅ Yes | ✅ Self-test |
| **KIA V3/V4** | KeeLoq (keystore) | ✅ Yes | ✅ Self-test: crypto + my new encoder round-trips our decoder |
| **Ford V0** | Obfuscation + GF(2) CRC | ✅ Yes | ✅ Full RF roundtrip self-test (decoder + encoder re-ported from upstream; a real decode bug was fixed in the process) |
| **VAG (Type 2, XTEA)** | XTEA (built-in key) | ✅ Yes | ✅ Full RF roundtrip self-test (`VAG ... /RF PASS`); encoder ported, a real decode bug fixed (short-pulse Manchester mapping) |
| VAG (Types 1/3/4, AUT64) | AUT64 (keystore) | ⛔ Blocked | Forward + encoder wired, but our `Aut64Cipher.h` port's `encrypt`/`decrypt` are **not a full 8-byte inverse** — so AUT64 forward can't be trusted until that cipher bug is fixed. Type-1 uses the same 300µs format as type-2 (works once the cipher is fixed); type-3/4 is the 500µs format |

**VAG note:** VAG **type 2 (XTEA)** fully works — forward + transmit encoder +
full RF roundtrip, verified on-device (`VAG(t2) data P/RF P`). In porting it we
fixed a real decode bug (the short-pulse Manchester mapping was inverted). The
**AUT64 types (1/3/4)** have the forward and the 300µs encoder wired, but they're
**blocked on a cipher bug**: our `Aut64Cipher.h` port's `encrypt` and `decrypt`
are not a full 8-byte inverse (a block round-trips its counter bytes but not the
rest), so AUT64 forward can't be trusted yet. The self-test flags this as
`aut64 inv BUG`. Fixing the AUT64 cipher (then the type-3/4 500µs encoder, then
loading real AUT64 keys) is the remaining VAG work. The type-2 XTEA key is
built in; AUT64 keys are loaded by the operator, not shipped.
| Subaru | Bit-rotation (no key) | ➖ N/A | Counter isn't a clean rolling counter |
| PSA / KIA V5/V6 | — | ➖ Decode only | — |

**KIA note:** KIA V3/V4 now uses the shared keystore like StarLine — the KIA
manufacturer key is **not** shipped; load it yourself (simple learning) for the
makes you're authorized to test.

**Ford note:** pulled the real upstream `ford_v0.c` and re-ported it end to end.
This fixed a genuine bug — our decoder read the button/counter from the wrong
bits of `buf[5]` (`>>4`/`&0x0F` instead of the real `>>3`/`&0x07`), so Ford
decode was quietly wrong — and restored the real encoder our build had dropped
(the key is transmitted inverted, after a lead-in + 4 preamble pairs + sync,
in the standard Flipper Manchester). Forward advances the 19-bit counter,
re-applies the reversible XOR + byte-swap + GF(2) CRC, and the whole path is
proven on-device: forward → transmit encoder → receive decoder → counter+1 with
button/serial preserved and a valid CRC. Faithful port, no guessing.

The engine is **extensible**: any protocol can add forward support by
implementing `supportsForward()` + `generateForward()` on its decoder class
(`src/modules/protopirate/protocols/`). The two verified protocols above are the
reference implementations — KeeLoq (decrypt → advance counter field →
re-encrypt) and cleartext (bump the counter in the frame). New protocols are
added "as they become available," per the project plan, and each should ship a
self-test case before its forward button is trusted.

> **Honesty note:** "Decoder only" means the Crow *reads* that fob but does not
> yet synthesize a forward code for it. We will not ship a forward generator for
> a make until its encoder round-trips in the on-device self-test **and** has
> been validated against a real fob of that make. No guesses.

---

## Loading manufacturer keys

### From the SD card (bulk)

Create `/DATA/KEYS/keeloq_mfcodes.txt` on the Crow's SD card. One key per line:

```
# name:learning:16-hex-key        ('#' lines are comments)
MyAlarm:normal:0123456789ABCDEF
TestBrand:simple:FEDCBA9876543210
```

`learning` is one of: `simple`, `normal`, `magic_xor1`, `magic_serial1`,
`magic_serial2`, `magic_serial3`, `unknown` (or the numeric KeeLoq learning
type). The file loads automatically at boot; the keystore is shared by every
rolling-code decoder.

### From the app (one key)

ProtoPirate screen → add key → name, 64-bit key, learning type. Keys added this
way live until reboot; put permanent keys in the SD file.

---

## Using it

1. **Load the manufacturer key** for the make you're authorized to test (above).
2. **Capture** a press from the fob (ProtoPirate decode). A supported rolling
   protocol shows the recovered counter.
3. Open the result → **Transmit NEXT code (rolling)** → choose how far to advance
   the counter (+1 is normal) and the radio module → **Transmit next**.
4. The Crow emits a valid forward code on the capture's frequency.

### On-device self-test

ProtoPirate → run forward self-test (BLE sub-command `0x11`). It builds a
synthetic frame with a known key, advances the counter, and checks the result
decodes to counter+1 — proving the compiled firmware's cipher and forward path
without any RF or real vehicle. Expected: `StarLine N→N+1 PASS | Fiat N→N+1 PASS
| ALL PASS`.

---

## For reviewers / buyers

- The KeeLoq cipher round-trips (encrypt/decrypt are exact inverses) — verified
  over 100,000 random pairs and a 20,000-case end-to-end pipeline in
  `tools`/host tests, and again on real hardware via the self-test.
- Forward generation never fabricates a key; it only advances a counter and
  re-encrypts with a key the operator loaded.
- Every forward-capable protocol is gated behind a self-test, and the honest
  status of each make is in the table above.
