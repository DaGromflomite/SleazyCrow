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

// Try every known protocol on a payload byte buffer. Returns true on first hit.
inline bool decodeAny(const uint8_t* b, int n, TpmsDecoded& out) {
    if (decodeFord(b, n, out)) return true;
    if (decodeToyota(b, n, out)) return true;
    return false;
}

// On-device self-test: validate the decoders against known vectors.
// Ford vector is rtl_433's documented layout with an INDEPENDENTLY verifiable
// additive checksum. Toyota vector is built to the spec (CRC self-consistent).
// Returns a bitmask: bit0 = Ford pass, bit1 = Toyota pass.
inline uint8_t selfTest() {
    uint8_t result = 0;
    // Ford: 12 34 56 78 7C 47 04 DB -> ID 0x12345678, 31.00 PSI, 15 C.
    {
        const uint8_t v[8] = {0x12, 0x34, 0x56, 0x78, 0x7C, 0x47, 0x04, 0xDB};
        TpmsDecoded d;
        if (decodeFord(v, 8, d) && d.id == 0x12345678u &&
            d.tempC == 15 && d.pressureKpa == psiToKpa(31.0f)) {
            result |= 0x01;
        }
    }
    // Toyota: build a valid packet to spec, append correct CRC, decode, verify.
    {
        uint8_t v[9] = {0x12, 0x34, 0x56, 0x78, 0x40, 0x1E, 0x00, 0x7F, 0x00};
        v[8] = crc8(v, 8, 0x07, 0x80);
        TpmsDecoded d;
        // pressure1 = (0x40<<1)|(0x1E>>7) = 0x80 = 128 -> 128*0.25-7 = 25 PSI
        // temp = (0x1E<<1)|(0x00>>7) = 0x3C = 60 -> 60-40 = 20 C
        if (decodeToyota(v, 9, d) && d.id == 0x12345678u &&
            d.tempC == 20 && d.pressureKpa == psiToKpa(25.0f)) {
            result |= 0x02;
        }
    }
    return result;
}

} // namespace tpms

#endif // TPMS_DECODERS_H
