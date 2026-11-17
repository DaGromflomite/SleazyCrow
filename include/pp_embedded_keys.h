#pragma once
// ===========================================================================
// ProtoPirate — keys compiled into the firmware (ship-with-firmware keystore)
// ===========================================================================
//
// WHAT THIS IS
//   Keys listed here are built into the firmware and loaded at boot, so the
//   key-fob next-code feature works without the operator having to put a file on
//   the SD card. The SD file (/DATA/KEYS/keeloq_mfcodes.txt) and the app's
//   add-key screen still work and add on top of these.
//
// WHAT SHIPS BY DEFAULT
//   - VAG Type-2 (XTEA): its key is published and is already built into PPVag
//     (see TEA_KEY in PPVag.h). It needs no entry here and works out of the box.
//   - KeeLoq / AUT64 manufacturer keys are NOT shipped. They are not published in
//     the clear anywhere — every upstream (RocketGod ProtoPirate, the Flipper
//     firmwares) keeps them in an AES-encrypted keystore on purpose. So the table
//     below is empty on a stock build.
//
// HOW TO ADD YOUR OWN KEYS (authorized testing only)
//   Put keys you are authorized to use — for a vehicle you own, or one you have
//   written permission to test — in PP_EMBEDDED_KEELOQ_KEYS below, then rebuild.
//   Format matches the SD file: a name, the learning type, and the 64-bit key.
//
//   learning is one of: PP_SIMPLE, PP_NORMAL, PP_MAGIC_XOR1, PP_MAGIC_SERIAL1,
//                       PP_MAGIC_SERIAL2, PP_MAGIC_SERIAL3, PP_UNKNOWN
//
//   Example (made-up key, replace with your own):
//       { "MyStarLine", 0x0123456789ABCDEFull, PP_NORMAL },
//
// AUTHORIZED TESTING ONLY. Keys let the reader recover a fob's counter and build
// the next rolling code. Only add keys for fobs and vehicles you own or are
// permitted to test.

#include <cstdint>

// Learning-type shorthands (mirror keeloq::LearningType).
#define PP_UNKNOWN        0
#define PP_SIMPLE         1
#define PP_NORMAL         2
#define PP_MAGIC_XOR1     3
#define PP_MAGIC_SERIAL1  4
#define PP_MAGIC_SERIAL2  5
#define PP_MAGIC_SERIAL3  6

struct PpEmbeddedKey {
    const char* name;
    uint64_t    key;
    uint8_t     learning;
};

// --- KeeLoq-type manufacturer/learning keys built into the firmware ---------
// Empty by default (see note above). Add your own authorized keys here.
static const PpEmbeddedKey PP_EMBEDDED_KEELOQ_KEYS[] = {
    // { "MyStarLine", 0x0123456789ABCDEFull, PP_NORMAL },
};
static const unsigned PP_EMBEDDED_KEELOQ_KEY_COUNT =
    sizeof(PP_EMBEDDED_KEELOQ_KEYS) / sizeof(PP_EMBEDDED_KEELOQ_KEYS[0]);
