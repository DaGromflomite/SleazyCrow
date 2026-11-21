#pragma once
/**
 * @file PPKiaV5.h
 * @brief Kia V5 key fob protocol decoder (decode only).
 *
 * Manchester encoding: te_short=400µs, te_long=800µs, 64-bit.
 * Custom "mixer" cipher (18 rounds) for counter decryption.
 * Ported from ProtoPirate kia_v5.c
 */

#include "PPProtocol.h"

class PPKiaV5 : public PPProtocol {
public:
    static constexpr const char* PROTOCOL_NAME = "Kia V5";

    const char* getName() const override { return PROTOCOL_NAME; }
    bool canEmulate() const override { return false; }  // Decode only

    const PPTimingConst& getTiming() const override {
        static const PPTimingConst t = {400, 800, 150, 64};
        return t;
    }

    void reset() override {
        resetDecoder();
        savedKey_ = 0;
        manchState_ = MS_Mid1;
    }

    // Faithful port of subghz_protocol_decoder_kia_v5_feed (ProtoPirate): the
    // data is STANDARD Flipper Manchester (not the half-pulse tracker the old
    // code used), with no implied leading bit. 64-bit key saved at bit 64, up
    // to 67 bits consumed; the frame ends on an out-of-range pulse.
    bool feed(bool level, uint32_t duration) override {
        const uint32_t TE_SHORT = 400, TE_LONG = 800, TE_DELTA = 150;
        bool isShort = DURATION_DIFF(duration, TE_SHORT) < TE_DELTA;
        bool isLong  = DURATION_DIFF(duration, TE_LONG)  < TE_DELTA;

        switch (parserStep_) {
        case StepReset:
            if (level && isShort) {
                parserStep_ = StepCheckPreamble;
                teLast_ = duration;
                headerCount_ = 1;
                decodeCountBit_ = 0;
                decodeData_ = 0;
                manchState_ = MS_Mid1;
            }
            break;

        case StepCheckPreamble:
            if (level) {
                if (isLong) {
                    if (headerCount_ > 40) {
                        parserStep_ = StepData;
                        decodeCountBit_ = 0;
                        decodeData_ = 0;
                        savedKey_ = 0;
                        headerCount_ = 0;
                    } else {
                        teLast_ = duration;
                    }
                } else if (isShort) {
                    teLast_ = duration;
                } else {
                    parserStep_ = StepReset;
                }
            } else {
                bool prevShort = DURATION_DIFF(teLast_, TE_SHORT) < TE_DELTA;
                bool prevLong  = DURATION_DIFF(teLast_, TE_LONG)  < TE_DELTA;
                if ((isShort && prevShort) || (isLong && prevShort) || prevLong) {
                    headerCount_++;
                } else {
                    parserStep_ = StepReset;
                }
                teLast_ = duration;
            }
            break;

        case StepData: {
            int event;
            if (isShort)      event = level ? ME_ShortHigh : ME_ShortLow;
            else if (isLong)  event = level ? ME_LongHigh  : ME_LongLow;
            else {
                parserStep_ = StepReset;
                return (decodeCountBit_ >= 64) ? extractData() : false;
            }
            if (decodeCountBit_ <= 66) {
                bool bit;
                if (manchAdvance(event, bit)) {
                    decodeData_ = (decodeData_ << 1) | (bit ? 1 : 0);
                    decodeCountBit_++;
                    if (decodeCountBit_ == 64) { savedKey_ = decodeData_; decodeData_ = 0; }
                }
            }
            teLast_ = duration;
            break;
        }
        }
        return false;
    }

private:
    uint64_t savedKey_ = 0;

    enum {
        StepReset = 0,
        StepCheckPreamble,
        StepData,
    };

    // Standard Flipper Manchester (same table Ford/Kia V1 use).
    enum { ME_ShortLow = 0, ME_ShortHigh = 2, ME_LongLow = 4, ME_LongHigh = 6, ME_Reset = 8 };
    enum { MS_Start1 = 0, MS_Mid1 = 1, MS_Mid0 = 2, MS_Start0 = 3 };
    uint8_t manchState_ = MS_Mid1;
    bool manchAdvance(int event, bool& bit) {
        static const uint8_t trans[4] = {0x01, 0x91, 0x9B, 0xFB};
        if (event == ME_Reset) { manchState_ = MS_Mid1; return false; }
        uint8_t ns = (uint8_t)((trans[manchState_] >> event) & 0x3);
        if (ns == manchState_) { manchState_ = MS_Mid1; return false; }
        bool res = false;
        if (ns == MS_Mid0) { bit = false; res = true; }
        else if (ns == MS_Mid1) { bit = true; res = true; }
        manchState_ = ns;
        return res;
    }

    /// Kia V5 "mixer" cipher decode (18 rounds, 8 steps each)
    static uint16_t mixerDecode(uint32_t encrypted, uint64_t key) {
        uint8_t s[4];
        s[0] = (encrypted >> 0) & 0xFF;
        s[1] = (encrypted >> 8) & 0xFF;
        s[2] = (encrypted >> 16) & 0xFF;
        s[3] = (encrypted >> 24) & 0xFF;

        uint8_t k[8];
        for (int i = 0; i < 8; i++) {
            k[i] = (key >> (i * 8)) & 0xFF;
        }

        // 18 rounds of mixing, reverse direction
        for (int round = 17; round >= 0; round--) {
            uint8_t ki = k[round % 8];

            // 8 steps of unmixing
            s[3] ^= s[2];
            s[2] ^= s[1];
            s[1] ^= s[0];
            s[0] ^= ki;

            // Rotate right
            uint8_t tmp = s[3];
            s[3] = s[2];
            s[2] = s[1];
            s[1] = s[0];
            s[0] = tmp;
        }

        return (uint16_t)((s[1] << 8) | s[0]);
    }

    bool extractData() {
        result_.data = savedKey_;
        result_.dataBits = 64;
        result_.protocolName = PROTOCOL_NAME;

        // yek = the WHOLE 64-bit key bit-reversed (bit i -> bit 63-i), which also
        // reverses byte order. The old code reversed each byte in place (wrong).
        uint64_t yek = 0;
        for (int i = 0; i < 64; i++)
            if (savedKey_ & (1ULL << i)) yek |= (1ULL << (63 - i));

        result_.serial  = (uint32_t)((yek >> 32) & 0x0FFFFFFF);
        result_.button  = (yek >> 60) & 0x0F;
        uint32_t encrypted = (uint32_t)(yek & 0xFFFFFFFF);
        result_.counter = mixerDecode(encrypted, kiaV5Key_);
        result_.crc     = decodeData_ & 0x07;  // 3-bit CRC from overflow
        result_.crcValid = true;  // No standard CRC validation
        result_.canEmulate = false;
        result_.encrypted = true;

        parserStep_ = StepReset;
        return true;
    }

    static uint64_t kiaV5Key_;

public:
    static void setKey(uint64_t key) { kiaV5Key_ = key; }
};

inline uint64_t PPKiaV5::kiaV5Key_ = 0;

PP_REGISTER_PROTOCOL(PPKiaV5)
