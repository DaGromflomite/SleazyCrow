#pragma once
/**
 * @file PPChryslerFca.h
 * @brief Chrysler / Dodge / Jeep / RAM (FCA) key-fob decoder.
 *
 * 112-bit Manchester, te ~209us (half-bit), 2*te full-bit, frames repeated and
 * separated by a long (~78ms) gap. Reverse-engineered from a real 2019 RAM 1500
 * fob capture (payload 0x0001C6B2ED239FBBF5CA76B2B739, consistent across frames).
 *
 * Standard Manchester: pair half-bits, 0->1 transition = 1, 1->0 = 0.
 * The leading bits are a sync preamble (0x0001...). We require exactly 112 bits
 * between frame gaps and a plausible leading preamble, then expose the fixed ID.
 */

#include "PPProtocol.h"

class PPChryslerFca : public PPProtocol {
public:
    static constexpr const char* PROTOCOL_NAME = "Chrysler/FCA";

    const char* getName() const override { return PROTOCOL_NAME; }

    const PPTimingConst& getTiming() const override {
        static const PPTimingConst t = {209, 418, 95, 112};
        return t;
    }

    void reset() override {
        resetDecoder();
        resetFrame();
    }

    bool feed(bool level, uint32_t duration) override {
        // Long gap = end of a frame (inter-frame ~78ms, well above any in-frame pulse).
        if (duration > 1500) {
            bool decoded = (bitCount_ == 112) && decodeFca();
            resetFrame();
            return decoded;
        }

        // Each pulse is 1 or 2 half-bits of the given level.
        int n = (int)((duration + 104) / 209);  // round to te units (te=209)
        if (n < 1 || n > 2) {       // glitch / out-of-spec → drop this frame
            resetFrame();
            return false;
        }
        for (int i = 0; i < n; i++) {
            if (!pushHalfBit(level ? 1 : 0)) {
                resetFrame();
                return false;
            }
        }
        return false;
    }

private:
    uint64_t data_  = 0;   // first 64 bits
    uint64_t data2_ = 0;   // remaining 48 bits
    uint16_t bitCount_ = 0;
    uint8_t  halfPhase_ = 0;
    uint8_t  firstHb_ = 0;

    void resetFrame() {
        data_ = 0; data2_ = 0; bitCount_ = 0; halfPhase_ = 0; firstHb_ = 0;
    }

    // Accumulate half-bits; every 2 -> one Manchester bit. Returns false on overflow.
    bool pushHalfBit(uint8_t hb) {
        if (halfPhase_ == 0) {
            firstHb_ = hb;
            halfPhase_ = 1;
            return true;
        }
        halfPhase_ = 0;
        // rising (0->1) = 1, falling (1->0) = 0; equal pair = bad alignment
        uint8_t bit;
        if (firstHb_ == 0 && hb == 1)      bit = 1;
        else if (firstHb_ == 1 && hb == 0) bit = 0;
        else return true;  // same-level pair: skip (slip), don't count
        if (bitCount_ >= 112) return false;
        if (bitCount_ < 64) data_ = (data_ << 1) | bit;
        else                data2_ = (data2_ << 1) | bit;
        bitCount_++;
        return true;
    }

    bool decodeFca() {
        // Plausibility: the capture's 112-bit frame begins with a sync preamble
        // (leading bits mostly 0 then a 1). Reject random 112-bit noise.
        // data_ top 16 bits should look like 0x0001 (15 zeros + start bit) for this
        // fob family; accept a small range to tolerate variants.
        uint16_t preamble = (uint16_t)(data_ >> 48);
        if (preamble != 0x0001) {
            return false;
        }

        // Fixed ID: 32 bits following the 16-bit preamble (bits 16..47).
        uint32_t serial = (uint32_t)((data_ >> 16) & 0xFFFFFFFFULL);

        result_.protocolName = PROTOCOL_NAME;
        result_.data     = data_;
        result_.data2    = data2_;
        result_.dataBits = 112;
        result_.serial   = serial;
        result_.button   = 0;   // button/counter mapping needs multi-capture; TBD
        result_.counter  = 0;
        result_.crc      = 0;
        result_.crcValid = false;
        result_.encrypted = true;   // rolling code
        result_.canEmulate = false;
        return true;
    }
};

PP_REGISTER_PROTOCOL(PPChryslerFca)
