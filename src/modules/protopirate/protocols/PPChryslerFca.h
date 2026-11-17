#pragma once
/**
 * @file PPChryslerFca.h
 * @brief Chrysler / Dodge / Jeep / RAM (FCA) key-fob decoder + next-code.
 *
 * Full port of the upstream ProtoPirate Chrysler V0 protocol (RocketGod fork).
 * KEYLESS: the "key" fed to the obfuscation transform is the 6-bit frame counter
 * (seed), not a secret — so decode AND next-code work with no manufacturer key.
 *
 * On-air: PWM, two 80-bit subframes (A: counter, B: second serial), each preceded
 * by a 24-pair preamble and separated by long gaps. A bit = HIGH(short|one_short)
 * + LOW(long_b|long_a); the decoded bit is inverted. 300us te.
 *
 * Verified: the encode->decode->next-code->decode roundtrip was proven over 2000
 * random frames in an offline model before this port. Authorized testing only.
 */

#include "PPProtocol.h"
#include <cstring>

class PPChryslerFca : public PPProtocol {
public:
    static constexpr const char* PROTOCOL_NAME = "Chrysler/FCA";

    const char* getName() const override { return PROTOCOL_NAME; }
    bool canEmulate() const override { return true; }
    bool supportsForward() const override { return true; }

    // Test helper: build a synthetic captured-frame result from a plain block.
    static PPDecodeResult makeTestFrame(const uint8_t plain[9], uint8_t counter, uint8_t button) {
        uint8_t ca = (uint8_t)(counter & 0x3F);
        if (ca & 1) ca = (uint8_t)((ca - 1) & 0x3F);
        uint8_t pa[10]; buildPayload(plain, ca, button, 0, pa);
        PPDecodeResult r; memset(&r, 0, sizeof(r));
        r.protocolName = PROTOCOL_NAME;
        r.data = bytesToU64(pa);
        r.data2 = (uint16_t)(((uint16_t)pa[8] << 8) | pa[9]);
        return r;
    }

    const PPTimingConst& getTiming() const override {
        static const PPTimingConst t = {300, 3700, 150, 80};
        return t;
    }

    void reset() override {
        resetDecoder();
        step_ = 0; pbc_ = 0; teLast_ = 0; data_ = 0; data2_ = 0; count_ = 0;
    }

    // ── Decode state machine (feed) ───────────────────────────────
    bool feed(bool level, uint32_t duration) override {
        switch (step_) {
        case 0:  // Reset
            if (level && isShort(duration)) { pbc_ = 0; teLast_ = duration; step_ = 1; }
            return false;
        case 1:  // Seek (preamble)
            if (level) { teLast_ = duration; return false; }
            if (isLongMark(duration)) {
                if (isShort(teLast_)) pbc_++;
                else if (pbc_ > 0x0F) { data_ = 1; data2_ = 0; count_ = 1; step_ = 2; }
                else { pbc_ = 0; }
                return false;
            }
            if (duration > TE_GAP && pbc_ > 0x0F) {
                data_ = 0; data2_ = 0; count_ = 0; step_ = 2; return false;
            }
            step_ = 0; pbc_ = 0; return false;
        case 2: { // Data
            if (level) { teLast_ = duration; return false; }
            if (duration > TE_GAP) {
                bool done = (count_ > 0x4F) && commitDecode();
                step_ = 0; pbc_ = 0;
                return done;
            }
            int bitValue;
            if (teLast_ < TE_SHORT) {
                if (!isShort(teLast_) || !isLongMark(duration)) {
                    bool done = (count_ > 0x4F) && commitDecode();
                    step_ = 0; pbc_ = 0; return done;
                }
                bitValue = 1;
            } else {
                if (teLast_ > 0x2EE || !isLongMark(duration)) {
                    bool done = (count_ > 0x4F) && commitDecode();
                    step_ = 0; pbc_ = 0; return done;
                }
                bitValue = isShort(teLast_) ? 1 : 0;
            }
            uint8_t bit = (uint8_t)(bitValue ^ 1);
            if (count_ <= 0x3F) { data_ = (data_ << 1) | bit; count_++; }
            else {
                data2_ = (uint16_t)(((data2_ << 1) | bit) & 0xFFFF); count_++;
                if (count_ == 0x50) {
                    bool done = commitDecode();
                    data_ = 0; data2_ = 0; count_ = 0; step_ = 0; pbc_ = 0;
                    return done;
                }
            }
            return false;
        }
        }
        step_ = 0; return false;
    }

    // ── Encoder: build the two-subframe PWM frame from a result ────
    std::vector<PPPulse> generatePulseData(const PPDecodeResult& r) override {
        uint8_t seed, button, plain[9], header;
        parseFrame(r.data, (uint16_t)r.data2, seed, button, plain, header);
        uint8_t counterA = (uint8_t)(seed & 0x3F);
        if (counterA & 1) counterA = (uint8_t)((counterA - 1) & 0x3F);
        uint8_t counterB = (counterA == 0) ? 0x3F : (uint8_t)(counterA - 1);
        uint8_t pa[10], pb[10];
        buildPayload(plain, counterA, button, header, pa);
        buildPayload(plain, counterB, button, header, pb);
        return buildUpload(pa, pb);
    }

    // ── Next-code: advance the frame counter, re-encode ───────────
    bool generateForward(const PPDecodeResult& in, int32_t delta,
                         PPDecodeResult& out) override {
        uint8_t seed, button, plain[9], header;
        parseFrame(in.data, (uint16_t)in.data2, seed, button, plain, header);
        uint8_t counterA = (uint8_t)((seed + delta) & 0x3F);
        if (counterA & 1) counterA = (uint8_t)((counterA - 1) & 0x3F);
        uint8_t pa[10];
        buildPayload(plain, counterA, button, header, pa);
        out = in;
        out.data  = bytesToU64(pa);
        out.data2 = (uint16_t)(((uint16_t)pa[8] << 8) | pa[9]);
        out.counter = counterA;
        out.protocolName = PROTOCOL_NAME;
        return true;
    }

private:
    uint8_t  step_ = 0, pbc_ = 0;
    uint32_t teLast_ = 0;
    uint64_t data_ = 0;
    uint16_t data2_ = 0;
    uint16_t count_ = 0;

    static constexpr uint32_t TE_SHORT = 300, TE_DELTA = 150;
    static constexpr uint32_t TE_LONG_A = 3400, TE_LONG_B = 3700, TE_LONG_DELTA = 400;
    static constexpr uint32_t TE_GAP = 8000, TE_ONE_SHORT = 600, FRAME_GAP = 15600;
    static constexpr int PREAMBLE_PAIRS = 24;

    static uint32_t dd(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }
    static bool isShort(uint32_t d) { return dd(d, TE_SHORT) <= TE_DELTA; }
    static bool isLongMark(uint32_t d) {
        return dd(d, TE_LONG_A) <= TE_LONG_DELTA || dd(d, TE_LONG_B) <= TE_LONG_DELTA;
    }
    static uint8_t reverse6(uint32_t v) {
        uint8_t o = 0; for (int i = 0; i < 6; i++) { o = (uint8_t)((o << 1) | (v & 1)); v >>= 1; } return o;
    }
    static const uint8_t* xorTable() {
        static const uint8_t t[16] = {0x0F,0x02,0x40,0x0C,0x30,0x0E,0x70,0x08,
                                      0x10,0x0A,0x50,0xF4,0x2F,0xF6,0x6F,0xF0};
        return t;
    }
    static void transform(const uint8_t in[9], uint8_t out[9], uint8_t key, uint8_t button) {
        uint8_t mask = xorTable()[key & 0x0F];
        if (button == 1) mask ^= (key & 1) ? 0xF0 : 0x0F;
        for (int i = 0; i < 9; i++) out[i] = (uint8_t)(in[i] ^ mask);
    }
    static void u64ToBytes(uint64_t v, uint8_t b[8]) { for (int i = 0; i < 8; i++) b[i] = (uint8_t)(v >> (8 * (7 - i))); }
    static uint64_t bytesToU64(const uint8_t b[10]) { uint64_t v = 0; for (int i = 0; i < 8; i++) v = (v << 8) | b[i]; return v; }
    static uint8_t payloadBit(const uint8_t p[10], uint8_t i) { return (p[i >> 3] >> (7 - (i & 7))) & 1; }

    static void buildPayload(const uint8_t plain[9], uint8_t counter, uint8_t button,
                             uint8_t headerLow2, uint8_t out[10]) {
        uint8_t tr[9]; transform(plain, tr, counter, button);
        out[0] = (uint8_t)((reverse6(counter) << 2) | (headerLow2 & 0x03));
        memcpy(&out[1], tr, 9);
    }
    std::vector<PPPulse> buildUpload(const uint8_t pa[10], const uint8_t pb[10]) {
        std::vector<PPPulse> u;
        auto E = [&](bool lvl, int32_t d) { u.push_back({ lvl ? d : -d }); };
        auto frame = [&](const uint8_t p[10]) {
            for (int i = 0; i < PREAMBLE_PAIRS; i++) { E(true, TE_SHORT); E(false, TE_LONG_B); }
            E(true, TE_SHORT); E(false, FRAME_GAP);
            for (uint8_t b = 0; b < 80; b++) {
                bool v = payloadBit(p, b);
                E(true, v ? TE_ONE_SHORT : TE_SHORT); E(false, v ? TE_LONG_A : TE_LONG_B);
            }
            E(true, TE_SHORT); E(false, FRAME_GAP);
        };
        frame(pa); frame(pb);
        return u;
    }

    // Decode the captured frame (data_/data2_) -> seed, button, plain block.
    void parseFrame(uint64_t data, uint16_t key2, uint8_t& seed, uint8_t& button,
                    uint8_t plain[9], uint8_t& header) {
        uint8_t key[8]; u64ToBytes(data, key);
        header = (uint8_t)(key[0] & 0x03);
        seed = reverse6((uint32_t)(key[0] >> 2));
        uint8_t b1x6 = (uint8_t)(key[6] ^ key[1]);
        bool msb = (key[0] & 0x80) != 0;
        if (msb) {
            uint8_t k2l = (uint8_t)(key2 & 0xFF);
            button = (((uint8_t)(k2l ^ key[4])) == 0x10) ? 2 : 1;
        } else {
            button = 1;
            if (((uint8_t)(key[1] ^ 0xC3)) == key[5]) {
                if (b1x6 == 0x08) button = 2;
            } else if (b1x6 == 0x08) button = 2;
        }
        uint8_t enc[9] = { key[1], key[2], key[3], key[4], key[5], key[6], key[7],
                           (uint8_t)(key2 >> 8), (uint8_t)(key2 & 0xFF) };
        transform(enc, plain, seed, button);
    }

    bool commitDecode() {
        uint8_t seed, button, plain[9], header;
        parseFrame(data_, data2_, seed, button, plain, header);
        // counter (32-bit) lives in the decoded block for the even-seed subframe
        uint32_t counter32 = ((uint32_t)plain[0] << 24) | ((uint32_t)plain[1] << 16) |
                             ((uint32_t)plain[2] << 8) | plain[3];
        uint32_t serial = ((uint32_t)plain[0] << 16) | ((uint32_t)plain[1] << 8) | plain[2];

        result_.protocolName = PROTOCOL_NAME;
        result_.data      = data_;
        result_.data2     = data2_;
        result_.dataBits  = 80;
        result_.serial    = serial;
        result_.button    = button;
        result_.counter   = (seed & 1) ? counter32 : seed;  // phase vs block counter
        result_.crc       = 0;
        result_.crcValid  = true;   // keyless obfuscation: no CRC gate
        result_.encrypted = false;
        result_.canEmulate = true;
        return true;
    }
};

PP_REGISTER_PROTOCOL(PPChryslerFca)
