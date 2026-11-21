#pragma once
/**
 * @file PPSubaru.h
 * @brief Subaru key fob protocol decoder/encoder.
 *
 * PWM encoding: te_short=800µs, te_long=1600µs, 64-bit.
 * Counter decryption via bit-rotation scheme.
 * Ported from ProtoPirate subaru.c
 */

#include "PPProtocol.h"

class PPSubaru : public PPProtocol {
public:
    static constexpr const char* PROTOCOL_NAME = "Subaru";

    const char* getName() const override { return PROTOCOL_NAME; }
    bool canEmulate() const override { return true; }

    const PPTimingConst& getTiming() const override {
        static const PPTimingConst t = {800, 1600, 200, 64};
        return t;
    }

    void reset() override {
        resetDecoder();
        bitCount_ = 0;
        key_ = 0;
        memset(keyBytes_, 0, sizeof(keyBytes_));
    }

    // Faithful port of subghz_protocol_decoder_subaru_feed (ProtoPirate).
    // The bit value comes from the LOW+HIGH *pair*, not the HIGH alone:
    //   short LOW -> long  HIGH = 0
    //   long  LOW -> short HIGH = 1
    // (Our old code read the HIGH only, which produced a 1-bit-shifted key.)
    bool feed(bool level, uint32_t duration) override {
        const uint32_t TE_SHORT = 800, TE_LONG = 1600, TE_DELTA = 200;
        const uint32_t GAP_US = 2800, SYNC_US = 2800, GAP_SYNC_DELTA = 800;
        const uint16_t PREAMBLE_MIN = 0x15;

        switch (parserStep_) {
        case StepReset:
            if (level && DURATION_DIFF(duration, TE_LONG) < TE_DELTA) {
                key_ = 0; bitCount_ = 0; headerCount_ = 0;
                teLast_ = duration;
                parserStep_ = StepCheckPreamble;
            }
            break;

        case StepCheckPreamble:
            if (level) return false;
            if (DURATION_DIFF(duration, TE_LONG) < TE_DELTA &&
                DURATION_DIFF(teLast_, TE_LONG) < TE_DELTA) {
                headerCount_++;
                return false;
            }
            if (headerCount_ >= PREAMBLE_MIN &&
                DURATION_DIFF(duration, GAP_US) < GAP_SYNC_DELTA) {
                teLast_ = duration;
                parserStep_ = StepFoundGap;
            } else {
                parserStep_ = StepReset;
            }
            break;

        case StepFoundGap:
            if (level && DURATION_DIFF(duration, SYNC_US) < GAP_SYNC_DELTA &&
                DURATION_DIFF(teLast_, GAP_US) < GAP_SYNC_DELTA) {
                parserStep_ = StepFoundSync;
            } else {
                parserStep_ = StepReset;
            }
            break;

        case StepFoundSync:
            if (!level && (DURATION_DIFF(duration, TE_LONG) < TE_DELTA ||
                           DURATION_DIFF(duration, TE_SHORT) < TE_DELTA)) {
                teLast_ = duration;
                parserStep_ = StepSaveDuration;
            } else {
                parserStep_ = StepReset;
            }
            break;

        case StepSaveDuration: {
            uint8_t next = StepReset;
            if (level) {
                int bit = -1;
                if (DURATION_DIFF(duration, TE_LONG) < TE_DELTA) {
                    if (DURATION_DIFF(teLast_, TE_SHORT) < TE_DELTA) bit = 0;
                } else if (DURATION_DIFF(duration, TE_SHORT) < TE_DELTA) {
                    if (DURATION_DIFF(teLast_, TE_LONG) < TE_DELTA) bit = 1;
                }
                if (bit >= 0) {
                    key_ = (key_ << 1) | (uint64_t)bit;
                    bitCount_++;
                    next = StepFoundSync;
                }
            }
            parserStep_ = next;
            if (bitCount_ >= 64 && bitCount_ <= 80) {
                return processData();
            }
            break;
        }
        }
        return false;
    }

    std::vector<PPPulse> generatePulseData(const PPDecodeResult& r) override {
        std::vector<PPPulse> pulses;
        auto& t = getTiming();

        // Preamble: 80 long HIGH/LOW pairs.
        // Last LOW is extended to form the sync gap (>2500µs)
        // so it lands at an odd index (LOW position in the alternating scheme).
        for (int i = 0; i < 80; i++) {
            pulses.push_back({(int32_t)t.te_long});
            if (i < 79) {
                pulses.push_back({-(int32_t)t.te_long});
            } else {
                pulses.push_back({-4000});  // Sync gap (LOW)
            }
        }

        // Data bits (MSB first, 64 bits)
        for (int bit = 63; bit >= 0; bit--) {
            if ((r.data >> bit) & 1) {
                pulses.push_back({(int32_t)t.te_short});  // Short HIGH = 1
            } else {
                pulses.push_back({(int32_t)t.te_long});   // Long HIGH = 0
            }
            pulses.push_back({-(int32_t)t.te_short});
        }

        // End: short HIGH + long LOW gap to trigger processData
        pulses.push_back({(int32_t)t.te_short});
        pulses.push_back({-4000});
        return pulses;
    }

private:
    uint32_t bitCount_ = 0;
    uint64_t key_ = 0;
    uint8_t keyBytes_[8] = {};

    enum {
        StepReset = 0,
        StepCheckPreamble,
        StepFoundGap,
        StepFoundSync,
        StepSaveDuration,
    };

    static const char* getButtonName(uint8_t btn) {
        switch (btn) {
        case 1:  return "Lock";
        case 2:  return "Unlock";
        case 4:  return "Trunk";
        case 8:  return "Panic";
        default: return "Unknown";
        }
    }

    // Faithful port of subaru decode_fields (ProtoPirate subaru.c):
    // serial = key bytes [1..3]; button = byte0 low nibble; counter recovered
    // via the scattered-bit 'lo', a (4+lo)-step 24-bit left-rotation of serial
    // bytes, and two XOR masks that unscramble the 'hi' byte.
    bool processData() {
        uint8_t kb[8];
        for (int i = 0; i < 8; i++) kb[i] = (uint8_t)(key_ >> (8 * (7 - i)));

        static const uint8_t loTab[8][3] = {
            {4,0x40,0x01},{4,0x80,0x02},{5,0x01,0x04},{5,0x02,0x08},
            {6,0x01,0x10},{6,0x02,0x20},{5,0x40,0x40},{5,0x80,0x80}};
        uint8_t lo = 0;
        for (auto& e : loTab) if ((kb[e[0]] & e[1]) == 0) lo |= e[2];

        uint8_t reg_sh1 = (uint8_t)(((kb[7] & 0x0F) << 4) | (kb[5] & 0x0C) | ((kb[6] >> 6) & 0x03));
        uint8_t reg_sh2 = (uint8_t)(((kb[6] & 0x3C) << 2) | ((kb[7] >> 4) & 0x0F));

        uint8_t ser0 = kb[3], ser1 = kb[1], ser2 = kb[2];
        uint8_t rot = (uint8_t)(4 + lo);
        for (uint8_t i = 0; i < rot; i++) {
            uint8_t t = ser0;
            ser0 = (uint8_t)((ser0 << 1) | (ser1 >> 7));
            ser1 = (uint8_t)((ser1 << 1) | (ser2 >> 7));
            ser2 = (uint8_t)((ser2 << 1) | (t >> 7));
        }
        uint8_t t1 = (uint8_t)(ser1 ^ reg_sh1);
        uint8_t t2 = (uint8_t)(ser2 ^ reg_sh2);

        static const uint8_t hiTab[8][3] = {
            {1,0x10,0x04},{1,0x20,0x08},{2,0x80,0x02},{2,0x40,0x01},
            {1,0x01,0x40},{1,0x02,0x80},{2,0x08,0x20},{2,0x04,0x10}};
        uint8_t hi = 0;
        for (auto& e : hiTab) {
            uint8_t v = (e[0] == 1) ? t1 : t2;
            if ((v & e[1]) == 0) hi |= e[2];
        }

        result_.data = key_;
        result_.dataBits = (uint16_t)bitCount_;
        result_.protocolName = PROTOCOL_NAME;
        result_.serial = ((uint32_t)kb[1] << 16) | ((uint32_t)kb[2] << 8) | kb[3];
        result_.button = kb[0] & 0x0F;
        result_.counter = (uint16_t)(((uint16_t)hi << 8) | lo);
        result_.crc = 0;
        result_.crcValid = true;
        result_.canEmulate = true;
        result_.encrypted = false;

        parserStep_ = StepReset;
        key_ = 0; bitCount_ = 0;
        return true;
    }
};

PP_REGISTER_PROTOCOL(PPSubaru)
