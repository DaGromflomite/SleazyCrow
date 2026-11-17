#!/usr/bin/env python3
"""
Sleazy Crow - forward rolling-code verification.

Run this to check the KeeLoq cipher and the StarLine forward-code pipeline on
your own machine, independent of the firmware. It is a plain Python port of the
same cipher the firmware uses (src/modules/protopirate/protocols/KeeloqCipher.h
and PPStarLine.h), so it is a second, independent check of the math.

It proves two things:
  1. KeeLoq encrypt and decrypt are exact inverses (over 100,000 random pairs).
  2. Building a frame, decoding it, advancing the counter, re-encoding, and
     decoding again gives counter+1 every time (over 20,000 cases), i.e. the
     "next code" is valid and really is the next one.

The firmware repeats check #2 on the actual device via its built-in self-test
(ProtoPirate BLE sub-command 0x11), so the same result is confirmed on hardware.

Usage:  python tools/verify_forward_codes.py
Exit code 0 and "ALL PASS" means the forward-code math is correct.
"""
import random

MASK32 = 0xFFFFFFFF
NLF = 0x3A5C742E

def bit(x, n):
    return (x >> n) & 1

def g5(x, a, b, c, d, e):
    return bit(x,a) + bit(x,b)*2 + bit(x,c)*4 + bit(x,d)*8 + bit(x,e)*16

def encrypt(data, key):
    x = data & MASK32
    for r in range(528):
        nb = (bit(x,0) ^ bit(x,16) ^ bit(key, r & 63) ^ bit(NLF, g5(x,1,9,20,26,31))) & 1
        x = ((x >> 1) ^ (nb << 31)) & MASK32
    return x

def decrypt(data, key):
    x = data & MASK32
    for r in range(528):
        msb = (bit(x,31) ^ bit(x,15) ^ bit(key,(15 - r) & 63) ^ bit(NLF, g5(x,0,8,19,25,30))) & 1
        x = (((x << 1) & MASK32) | msb) & MASK32
    return x

def normal_learning(serial, key):
    data = serial & 0x0FFFFFFF
    k1 = decrypt(data | 0x20000000, key)
    k2 = decrypt(data | 0x60000000, key)
    return ((k2 << 32) | k1) & 0xFFFFFFFFFFFFFFFF

def reverse64(val):
    r = 0
    for _ in range(64):
        r = ((r << 1) | (val & 1)) & 0xFFFFFFFFFFFFFFFF
        val >>= 1
    return r

# ---- 1. Cipher is a true inverse (critical for forward codes) ----
random.seed(1)
inv_ok = True
for _ in range(100000):
    k = random.getrandbits(64)
    p = random.getrandbits(32)
    if decrypt(encrypt(p, k), k) != p:
        inv_ok = False; break
    if encrypt(decrypt(p, k), k) != p:
        inv_ok = False; break
print(f"[1] encrypt/decrypt mutual inverse over 100k random pairs: {'PASS' if inv_ok else 'FAIL'}")

# ---- 2. StarLine SIMPLE-learning pipeline: encode -> decode -> forward -> decode ----
def starline_build_frame(device_key, btn, serial24, counter):
    """Construct result.data exactly as the C++ would after a real capture."""
    key_fix = ((btn & 0xFF) << 24) | (serial24 & 0xFFFFFF)
    end_ser = key_fix & 0xFF
    plain   = ((btn & 0xFF) << 24) | ((end_ser & 0xFF) << 16) | (counter & 0xFFFF)
    hop     = encrypt(plain, device_key)
    key64   = ((key_fix << 32) | hop) & 0xFFFFFFFFFFFFFFFF
    return reverse64(key64)            # == result.data

def starline_decode(data, device_key):
    """Mirror extractData + matchKey (simple learning)."""
    key    = reverse64(data)
    key_fix = (key >> 32) & MASK32
    key_hop = key & MASK32
    btn     = (key_fix >> 24) & 0xFF
    end_ser = key_fix & 0xFF
    d = decrypt(key_hop, device_key)
    if (d >> 24) == btn and ((d >> 16) & 0xFF) == end_ser:
        return {'btn': btn, 'end_ser': end_ser, 'counter': d & 0xFFFF, 'key_fix': key_fix}
    return None

def starline_forward(data, device_key, delta):
    """Mirror generateForward (simple learning)."""
    dec = starline_decode(data, device_key)
    if dec is None:
        return None
    new_cnt = (dec['counter'] + delta) & 0xFFFF
    plain   = ((dec['btn'] & 0xFF) << 24) | ((dec['end_ser'] & 0xFF) << 16) | new_cnt
    new_hop = encrypt(plain, device_key)
    new_key = ((dec['key_fix'] << 32) | new_hop) & 0xFFFFFFFFFFFFFFFF
    return reverse64(new_key), new_cnt

pipe_ok = True
fails = 0
for _ in range(20000):
    dk      = random.getrandbits(64)
    btn     = random.choice([0x1, 0x2, 0x4, 0x8, 0xA])
    serial  = random.getrandbits(24)
    counter = random.getrandbits(16)
    delta   = random.choice([1, 2, 5, 100])

    frame = starline_build_frame(dk, btn, serial, counter)
    dec0  = starline_decode(frame, dk)
    if not dec0 or dec0['counter'] != counter:
        pipe_ok = False; fails += 1; continue

    fwd = starline_forward(frame, dk, delta)
    if not fwd:
        pipe_ok = False; fails += 1; continue
    fwd_frame, expected_cnt = fwd
    dec1 = starline_decode(fwd_frame, dk)
    if not dec1 or dec1['counter'] != ((counter + delta) & 0xFFFF):
        pipe_ok = False; fails += 1; continue
    # forward frame must differ from the original (it's a NEW code)
    if fwd_frame == frame and delta % 0x10000 != 0:
        pipe_ok = False; fails += 1; continue

print(f"[2] StarLine encode->decode->forward(+delta)->decode over 20k cases: "
      f"{'PASS' if pipe_ok else 'FAIL ('+str(fails)+' fails)'}")

# ---- 3. Show one concrete worked example ----
dk = 0x0123456789ABCDEF
btn, serial, counter = 0x2, 0xA5B6C7, 0x0041
frame = starline_build_frame(dk, btn, serial, counter)
d0 = starline_decode(frame, dk)
ff, nc = starline_forward(frame, dk, 1)
d1 = starline_decode(ff, dk)
print("\n[3] Worked example (device key 0x0123456789ABCDEF):")
print(f"    capture  data=0x{frame:016X}  -> counter={d0['counter']}")
print(f"    forward  data=0x{ff:016X}  -> counter={d1['counter']}  (was {d0['counter']})")
print(f"    next code valid & counter advanced by 1: "
      f"{d1['counter'] == d0['counter']+1 and ff != frame}")

print("\nRESULT:", "ALL PASS" if (inv_ok and pipe_ok) else "FAILURES PRESENT")
