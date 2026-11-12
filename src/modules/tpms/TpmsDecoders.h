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

// Try every known protocol on a payload byte buffer. Returns true on first hit.
// CRC/checksum validation makes a false match at a wrong alignment near-impossible.
inline bool decodeAny(const uint8_t* b, int n, TpmsDecoded& out) {
    if (decodeFord(b, n, out)) return true;
    if (decodeToyota(b, n, out)) return true;
    if (decodeHyundaiVDO(b, n, out)) return true;
    if (decodeSchraderEG53(b, n, out)) return true;
    if (decodeSchrader(b, n, out)) return true;
    return false;
}

// On-device self-test: validate every byte-decoder against known vectors.
// Ford uses rtl_433's documented INDEPENDENTLY verifiable additive checksum;
// the others are built to spec with a self-consistent CRC/checksum (confirms
// field extraction + the CRC implementation). Returns a bitmask:
// bit0 Ford, bit1 Toyota, bit2 Hyundai-VDO, bit3 Schrader, bit4 Schrader-EG53.
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
    return result;
}

} // namespace tpms

#endif // TPMS_DECODERS_H
