#ifndef TPMS_DECODERS_H
#define TPMS_DECODERS_H

#include <stdint.h>
#include <string.h>

/**
 * @file TpmsDecoders.h
 * @brief Byte-level TPMS protocol decoders, reimplemented from the PUBLIC rtl_433
 *        protocol layouts (not copied — GPL source read only for the field map).
 *
 * Each decoder takes the demodulated payload bytes and extracts the fixed 32-bit
 * sensor ID (+ pressure/temperature where the checksum/CRC validates). These are
 * pure functions so they can be confirmed against known test vectors on-device
 * (see TpmsModule self-test). The pulse->bytes demodulation is separate.
 *
 * Protocol ids (match BinaryTpmsHit.protocol):
 *   0 = generic fingerprint (no protocol matched)
 *   1 = Ford      (8 bytes, additive checksum)
 *   2 = Toyota    (9 bytes, CRC-8 poly 0x07 init 0x80, inverted-pressure check)
 */

struct TpmsDecoded {
    bool     ok = false;
    uint32_t id = 0;
    uint16_t pressureKpa = 0xFFFF; // 0xFFFF = unknown
    int8_t   tempC = -128;         // -128 = unknown
    uint8_t  protocol = 0;
};

namespace tpms {

inline uint16_t psiToKpa(float psi) {
    if (psi < 0) psi = 0;
    float kpa = psi * 6.89476f;
    if (kpa > 65000.0f) kpa = 65000.0f;
    return (uint16_t)(kpa + 0.5f);
}

// CRC-8, MSB-first, parameterized (used by Toyota: poly 0x07, init 0x80).
inline uint8_t crc8(const uint8_t* data, int len, uint8_t poly, uint8_t init) {
    uint8_t crc = init;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ poly) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

// ── Ford (rtl_433 tpms_ford): 8 payload bytes ──────────────────────────────
// ID = b0..b3 (big-endian). Checksum b7 = sum(b0..b6) & 0xFF.
// Pressure = ((b6 & 0x20) << 3 | b4) * 0.25 PSI. Temp (if b5&0x80==0) = (b5&0x7F)-56 C.
// Flag bits 0x80 and 0x10 of b6 must be 0.
inline bool decodeFord(const uint8_t* b, int n, TpmsDecoded& out) {
    if (n < 8) return false;
    uint8_t sum = 0;
    for (int i = 0; i < 7; i++) sum += b[i];
    if (sum != b[7]) return false;
    if (b[6] & 0x80) return false;
    if (b[6] & 0x10) return false;
    out.id = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
             ((uint32_t)b[2] << 8) | (uint32_t)b[3];
    if (out.id == 0 || out.id == 0xFFFFFFFFu) return false;
    float psi = (((b[6] & 0x20) << 3) | b[4]) * 0.25f;
    out.pressureKpa = psiToKpa(psi);
    if ((b[5] & 0x80) == 0) out.tempC = (int8_t)((b[5] & 0x7F) - 56);
    out.protocol = 1;
    out.ok = true;
    return true;
}

// ── Toyota (rtl_433 tpms_toyota, Pacific PMV-C210): 9 payload bytes ─────────
// ID = b0..b3 (big-endian). CRC-8 poly 0x07 init 0x80 over b0..b7 == b8.
// pressure1 = (b4&0x7F)<<1 | (b5>>7); must equal (b7 ^ 0xFF). PSI = p1*0.25 - 7.
// temp = (b5&0x7F)<<1 | (b6>>7); C = temp - 40.
inline bool decodeToyota(const uint8_t* b, int n, TpmsDecoded& out) {
    if (n < 9) return false;
    if (crc8(b, 8, 0x07, 0x80) != b[8]) return false;
    uint8_t pressure1 = (uint8_t)(((b[4] & 0x7F) << 1) | (b[5] >> 7));
    uint8_t pressure2 = (uint8_t)(b[7] ^ 0xFF);
    if (pressure1 != pressure2) return false;
    out.id = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
             ((uint32_t)b[2] << 8) | (uint32_t)b[3];
    if (out.id == 0 || out.id == 0xFFFFFFFFu) return false;
    float psi = pressure1 * 0.25f - 7.0f;
    out.pressureKpa = psiToKpa(psi);
    uint8_t tempRaw = (uint8_t)(((b[5] & 0x7F) << 1) | (b[6] >> 7));
    out.tempC = (int8_t)((int)tempRaw - 40);
    out.protocol = 2;
    out.ok = true;
    return true;
}

// ── Hyundai/Kia (Continental VDO, rtl_433 tpms_hyundai_vdo): 10 bytes ───────
// FSK Manchester. ID = b1..b4 big-endian. CRC-8 poly 0x07 init 0xAA over b0..b8.
// Pressure = b6 * 1.375 kPa. Temp = b7 - 50 C. (proto 3)
inline bool decodeHyundaiVDO(const uint8_t* b, int n, TpmsDecoded& out) {
    if (n < 10) return false;
    if (crc8(b, 9, 0x07, 0xAA) != b[9]) return false;
    out.id = ((uint32_t)b[1] << 24) | ((uint32_t)b[2] << 16) |
             ((uint32_t)b[3] << 8) | (uint32_t)b[4];
    if (out.id == 0 || out.id == 0xFFFFFFFFu) return false;
    uint16_t kpa = (uint16_t)(b[6] * 1.375f + 0.5f);
    out.pressureKpa = kpa;
    out.tempC = (int8_t)((int)b[7] - 50);
    out.protocol = 3;
    out.ok = true;
    return true;
}

// ── Schrader generic (rtl_433 schraeder): 8 bytes (after sync nibble) ───────
// OOK Manchester. 28-bit ID = (b1&0x0F)<<24 | b2<<16 | b3<<8 | b4.
// CRC-8 poly 0x07 init 0xF0 over b0..b6 == b7. Pressure b5*2.5 kPa, temp b6-50.
// (proto 4) FCC MRXGG4 — many Schrader-equipped vehicles.
inline bool decodeSchrader(const uint8_t* b, int n, TpmsDecoded& out) {
    if (n < 8) return false;
    if (crc8(b, 7, 0x07, 0xF0) != b[7]) return false;
    out.id = ((uint32_t)(b[1] & 0x0F) << 24) | ((uint32_t)b[2] << 16) |
             ((uint32_t)b[3] << 8) | (uint32_t)b[4];
    if (out.id == 0) return false;
    out.pressureKpa = (uint16_t)(b[5] * 2.5f + 0.5f);
    out.tempC = (int8_t)((int)b[6] - 50);
    out.protocol = 4;
    out.ok = true;
    return true;
}

// ── Schrader EG53MA4 (rtl_433): 10 bytes (after 40-bit discard) ─────────────
// OOK Manchester. 24-bit ID = b4,b5,b6. Pressure b7*2.5 kPa. Temp b8 (deg F raw).
// Checksum = sum(b0..b8) & 0xFF == b9. (proto 5) GM/Chevy, Opel/Saab/Vauxhall.
inline bool decodeSchraderEG53(const uint8_t* b, int n, TpmsDecoded& out) {
    if (n < 10) return false;
    uint8_t sum = 0;
    for (int i = 0; i < 9; i++) sum += b[i];
    if (sum != b[9]) return false;
    // sanity: these are never all-zero in a real packet
    if (!b[1] && !b[2] && !b[4] && !b[5] && !b[7] && !b[8]) return false;
    out.id = ((uint32_t)b[4] << 16) | ((uint32_t)b[5] << 8) | (uint32_t)b[6];
    if (out.id == 0 || out.id == 0xFFFFFF) return false;
    out.pressureKpa = (uint16_t)(b[7] * 2.5f + 0.5f);
    out.tempC = (int8_t)(((int)b[8] - 32) * 5 / 9);  // deg F -> C
    out.protocol = 5;
    out.ok = true;
    return true;
}

// ── Honda / Acura (rtl_433 tpms_honda): FSK Manchester, 8 bytes ────────────
// CRC-8/SMBUS poly 0x07 init 0x00 over b0..b6 == b7. ID = b2..b5 big-endian.
// Pressure = b0 * 0.2 PSI. Temp = b1 - 50 C. (proto 6)
inline bool decodeHonda(const uint8_t* b, int n, TpmsDecoded& out) {
    if (n < 8) return false;
    if (crc8(b, 7, 0x07, 0x00) != b[7]) return false;
    out.id = ((uint32_t)b[2] << 24) | ((uint32_t)b[3] << 16) |
             ((uint32_t)b[4] << 8) | (uint32_t)b[5];
    if (out.id == 0 || out.id == 0xFFFFFFFFu) return false;
    out.pressureKpa = psiToKpa(b[0] * 0.2f);
    out.tempC = (int8_t)((int)b[1] - 50);
    out.protocol = 6;
    out.ok = true;
    return true;
}

// ── TRW (rtl_433 tpms_trw, FCC GQ4-70T): OOK or FSK Manchester, 10 bytes ────
// CRC-8/SMBUS poly 0x07 init 0x00 over b0..b8 == b9. ID = b1..b4 big-endian.
// Pressure = b6 * 0.4 PSI. Temp = b7 - 50 C. (proto 7) cross-make: GM, Honda, FCA.
inline bool decodeTRW(const uint8_t* b, int n, TpmsDecoded& out) {
    if (n < 10) return false;
    if (crc8(b, 9, 0x07, 0x00) != b[9]) return false;
    out.id = ((uint32_t)b[1] << 24) | ((uint32_t)b[2] << 16) |
             ((uint32_t)b[3] << 8) | (uint32_t)b[4];
    if (out.id == 0 || out.id == 0xFFFFFFFFu) return false;
    out.pressureKpa = psiToKpa(b[6] * 0.4f);
    out.tempC = (int8_t)((int)b[7] - 50);
    out.protocol = 7;
    out.ok = true;
    return true;
}

// ── Jeep / FCA (rtl_433 tpms_jeep, Continental): FSK Manchester, 10 bytes ───
// XOR of b1..b9 == 0. ID = b1..b4 big-endian. Pressure = b6 * 2.728 kPa.
// Temp = b7 - 50 C. b6 and b7 must be non-zero. (proto 8) Jeep/Chrysler/Dodge/
// RAM (wire format shared with Citroën).
inline bool decodeJeep(const uint8_t* b, int n, TpmsDecoded& out) {
    if (n < 10) return false;
    uint8_t x = 0;
    for (int i = 1; i <= 9; i++) x ^= b[i];
    if (x != 0) return false;
    if (b[6] == 0 || b[7] == 0) return false;
    out.id = ((uint32_t)b[1] << 24) | ((uint32_t)b[2] << 16) |
             ((uint32_t)b[3] << 8) | (uint32_t)b[4];
    if (out.id == 0 || out.id == 0xFFFFFFFFu) return false;
    out.pressureKpa = (uint16_t)(b[6] * 2.728f + 0.5f);
    out.tempC = (int8_t)((int)b[7] - 50);
    out.protocol = 8;
    out.ok = true;
    return true;
}

// Try every known protocol on a payload byte buffer. Returns true on first hit.
// CRC/checksum validation makes a false match at a wrong alignment near-impossible.
// Reject IDs that are just a short repeating bit pattern (0x55555555,
// 0x24924924, 0xFFFFFF...). Those come from the preamble or from a garbled
// frame, and with weak 8-bit checksums tried at every bit alignment they can
// "validate" -- and repeat identically, so the repeat rule can't catch them
// (found replaying OTA captures). A real factory ID is never periodic.
inline bool plausibleId(uint32_t id, int bits) {
    uint32_t mask = bits >= 32 ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    id &= mask;
    if (id == 0 || id == mask) return false;
    for (int p = 1; p <= 4; p++) {
        // periodic with at most one stray bit
        if (__builtin_popcount((id ^ (id >> p)) & (mask >> p)) <= 1) return false;
    }
    // ...nor mostly one long run of 0s or 1s (0x000003FF, 0x80000000 seen in
    // replayed captures): reject a run >= 2/3 of the ID width.
    int run = 1, longest = 1;
    for (int i = 1; i < bits; i++) {
        bool same = ((id >> i) & 1u) == ((id >> (i - 1)) & 1u);
        run = same ? run + 1 : 1;
        if (run > longest) longest = run;
    }
    return longest * 3 < bits * 2;
}

inline int idBits(uint8_t protocol) {
    return protocol == 4 ? 28 : protocol == 5 ? 24 : 32;
}

// A real tyre reading is bounded: pressure 0..900 kPa (0..130 PSI, passenger +
// light-truck), temperature -50..100 C. 0xFFFF / -128 are the "unknown" sentinels
// and are allowed. A wrong-protocol cross-match at a valid alignment usually
// decodes to an out-of-range reading, so this rejects it where the checksum alone
// cannot (verified against a faithful demod model: it removes the residual
// cross-protocol ghosts that the weak 8-bit checksums let through).
inline bool plausibleReading(uint16_t kpa, int8_t tempC) {
    if (kpa != 0xFFFF && kpa > 900) return false;
    if (tempC != -128 && (tempC < -50 || tempC > 100)) return false;
    return true;
}

// family: 0 = any (self-test), 1 = FSK only (Ford/Toyota/Hyundai-VDO),
// 2 = OOK only (Schrader family). Only trying the decoders that match the
// modulation being received halves the odds of a garbled frame passing some
// protocol's 8-bit checksum by chance (seen OTA: an FSK frame "passed" GM EG53).
inline bool decodeAny(const uint8_t* b, int n, TpmsDecoded& out, int family = 0) {
    // Each decode must pass its checksum AND have a non-periodic ID AND a
    // physically plausible pressure/temperature.
    #define TPMS_ACCEPT (plausibleId(out.id, idBits(out.protocol)) && \
                         plausibleReading(out.pressureKpa, out.tempC))
    if (family != 2) {
        if (decodeFord(b, n, out) && TPMS_ACCEPT) return true;
        if (decodeToyota(b, n, out) && TPMS_ACCEPT) return true;
        if (decodeHyundaiVDO(b, n, out) && TPMS_ACCEPT) return true;
        if (decodeJeep(b, n, out) && TPMS_ACCEPT) return true;
    }
    if (family != 1) {
        if (decodeSchraderEG53(b, n, out) && TPMS_ACCEPT) return true;
        if (decodeSchrader(b, n, out) && TPMS_ACCEPT) return true;
    }
    // TRW is transmitted as OOK and FSK, so try it in either modulation family
    // (its 10-byte CRC-8 makes a chance match near-impossible).
    if (decodeTRW(b, n, out) && TPMS_ACCEPT) return true;
    // NOTE: Honda (decodeHonda, proto 6) is intentionally NOT dispatched here.
    // Its CRC-8/SMBUS init 0x00 is weak enough to cross-match other protocols'
    // frames at a valid alignment (proven in the faithful demod model). rtl_433
    // anchors Honda on a dedicated 23-bit marker; until that marker search exists
    // in the demod front-end, enabling Honda re-introduces cross-protocol ghosts.
    #undef TPMS_ACCEPT
    return false;
}

// On-device self-test: validate every byte-decoder against known vectors.
// Ford uses rtl_433's documented INDEPENDENTLY verifiable additive checksum;
// the others are built to spec with a self-consistent CRC/checksum (confirms
// field extraction + the CRC implementation). Returns a bitmask:
// bit0 Ford, bit1 Toyota, bit2 Hyundai-VDO, bit3 Schrader, bit4 Schrader-EG53,
// bit5 Honda, bit6 TRW.
inline uint8_t selfTest() {
    uint8_t result = 0;
    TpmsDecoded d;
    // Ford: 12 34 56 78 7C 47 04 DB -> ID 0x12345678, 31.00 PSI, 15 C.
    {
        const uint8_t v[8] = {0x12, 0x34, 0x56, 0x78, 0x7C, 0x47, 0x04, 0xDB};
        d = TpmsDecoded();
        if (decodeFord(v, 8, d) && d.id == 0x12345678u &&
            d.tempC == 15 && d.pressureKpa == psiToKpa(31.0f)) result |= 0x01;
    }
    // Toyota: pressure1=128 -> 25 PSI, temp raw 60 -> 20 C.
    {
        uint8_t v[9] = {0x12, 0x34, 0x56, 0x78, 0x40, 0x1E, 0x00, 0x7F, 0x00};
        v[8] = crc8(v, 8, 0x07, 0x80);
        d = TpmsDecoded();
        if (decodeToyota(v, 9, d) && d.id == 0x12345678u &&
            d.tempC == 20 && d.pressureKpa == psiToKpa(25.0f)) result |= 0x02;
    }
    // Hyundai-VDO: ID=b1..b4=0x0ABBCCDD, b6=160 -> 220 kPa, b7=70 -> 20 C.
    {
        uint8_t v[10] = {0x20, 0x0A, 0xBB, 0xCC, 0xDD, 0x00, 160, 70, 0x00, 0x00};
        v[9] = crc8(v, 9, 0x07, 0xAA);
        d = TpmsDecoded();
        if (decodeHyundaiVDO(v, 10, d) && d.id == 0x0ABBCCDDu &&
            d.tempC == 20 && d.pressureKpa == 220) result |= 0x04;
    }
    // Schrader generic: ID=(b1&0x0F)<<24|b2<<16|b3<<8|b4 = 0x0A345678, b5=88 ->220kPa, b6=70->20C.
    {
        uint8_t v[8] = {0x12, 0x0A, 0x34, 0x56, 0x78, 88, 70, 0x00};
        v[7] = crc8(v, 7, 0x07, 0xF0);
        d = TpmsDecoded();
        if (decodeSchrader(v, 8, d) && d.id == 0x0A345678u &&
            d.tempC == 20 && d.pressureKpa == 220) result |= 0x08;
    }
    // Schrader EG53 (GM): ID=b4..b6=0x123456, b7=88 ->220kPa, b8=68F ->20C.
    {
        uint8_t v[10] = {0x01, 0x02, 0x03, 0x04, 0x12, 0x34, 0x56, 88, 68, 0x00};
        uint8_t s = 0; for (int i = 0; i < 9; i++) s += v[i]; v[9] = s;
        d = TpmsDecoded();
        if (decodeSchraderEG53(v, 10, d) && d.id == 0x123456u &&
            d.pressureKpa == 220 && d.tempC == 20) result |= 0x10;
    }
    // Honda: b0=125 -> 25 PSI, b1=70 -> 20 C, ID=b2..b5=0x12345678.
    {
        uint8_t v[8] = {125, 70, 0x12, 0x34, 0x56, 0x78, 0x00, 0x00};
        v[7] = crc8(v, 7, 0x07, 0x00);
        d = TpmsDecoded();
        if (decodeHonda(v, 8, d) && d.id == 0x12345678u &&
            d.tempC == 20 && d.pressureKpa == psiToKpa(25.0f)) result |= 0x20;
    }
    // TRW: ID=b1..b4=0x12345678, b6=60 -> 24 PSI, b7=70 -> 20 C.
    {
        uint8_t v[10] = {0x00, 0x12, 0x34, 0x56, 0x78, 0x00, 60, 70, 0x00, 0x00};
        v[9] = crc8(v, 9, 0x07, 0x00);
        d = TpmsDecoded();
        if (decodeTRW(v, 10, d) && d.id == 0x12345678u &&
            d.tempC == 20 && d.pressureKpa == psiToKpa(24.0f)) result |= 0x40;
    }
    return result;
}

} // namespace tpms

#endif // TPMS_DECODERS_H
