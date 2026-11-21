#pragma once
/**
 * @file PPKiaV0.h
 * @brief Kia V0 key fob protocol decoder/encoder.
 *
 * PWM encoding: te_short=250µs, te_long=500µs, 61-bit, CRC8 poly 0x7F.
 * Ported from ProtoPirate kia_v0.c
 */

#include "PPProtocol.h"

class PPKiaV0 : public PPProtocol {
public:
    static constexpr const char* PROTOCOL_NAME = "Kia V0";

    const char* getName() const override { return PROTOCOL_NAME; }
    bool canEmulate() const override { return true; }

    const PPTimingConst& getTiming() const override {
        static const PPTimingConst t = {250, 500, 100, 61};
        return t;
    }

    void reset() override { resetDecoder(); }

    // Faithful port of subghz_protocol_decoder_kia_feed (ProtoPirate kia_v0.c,
    // Kia-type path). Data bits come from LOW+HIGH *pairs* (short/short=0,
    // long/long=1), NOT a single PWM pulse; the long-pair sync contributes two
    // leading '1' bits; the frame ends on a 700..1700us HIGH gap and ONLY
    // commits when the CRC-8 (poly 0x7F) checks. The old single-pulse PWM feed
    // without the two sync bits never completed a real 61-bit frame.
    bool feed(bool level, uint32_t duration) override {
        switch (parserStep_) {
        case StepReset:
            decodeData_ = 0; decodeCountBit_ = 0; headerCount_ = 0;
            if (level && isShort(duration)) {
                parserStep_ = StepPreamble;
                teLast_ = duration;
            }
            break;

        case StepPreamble:
            if (level) {
                if (isShort(duration) || isLong(duration)) teLast_ = duration;
                else parserStep_ = StepReset;
            } else if (isShort(duration) && isShort(teLast_)) {
                headerCount_++;                       // one short HIGH/LOW pair
            } else if (isLong(duration) && isLong(teLast_)) {
                if (headerCount_ > 14) {
                    decodeData_ = 0; decodeCountBit_ = 0;
                    addBit(1); addBit(1);             // sync = two leading 1 bits
                    parserStep_ = StepSaveDuration;
                } else {
                    parserStep_ = StepReset;
                }
            } else {
                parserStep_ = StepReset;
            }
            break;

        case StepSaveDuration:                        // expects the HIGH of a pair
            if (!level) { parserStep_ = StepReset; break; }
            if (isKiaGap(duration)) return finishAtGap();
            if (isShort(duration) || isLong(duration)) {
                teLast_ = duration;
                parserStep_ = StepCheckDuration;
            } else {
                parserStep_ = StepReset;
            }
            break;

        case StepCheckDuration:                       // expects the LOW of a pair
            if (level) { parserStep_ = StepReset; break; }
            if (isShort(teLast_) && isShort(duration)) {
                addBit(0); parserStep_ = StepSaveDuration;
            } else if (isLong(teLast_) && isLong(duration)) {
                addBit(1); parserStep_ = StepSaveDuration;
            } else {
                parserStep_ = StepReset;
            }
            break;
        }
        return false;
    }

    bool finishAtGap() {
        bool ok = (decodeCountBit_ == 61) && verifyCrcPoly(decodeData_);
        if (ok) return extractData();
        parserStep_ = StepReset;
        return false;
    }

    std::vector<PPPulse> generatePulseData(const PPDecodeResult& r) override {
        std::vector<PPPulse> pulses;
        auto& t = getTiming();

        // Preamble: 32 short pairs
        for (int i = 0; i < 32; i++) {
            pulses.push_back({(int32_t)t.te_short});
            pulses.push_back({-(int32_t)t.te_short});
        }

        // Sync: long HIGH + long LOW
        pulses.push_back({(int32_t)t.te_long});
        pulses.push_back({-(int32_t)t.te_long});

        // Data bits (MSB first, 61 data bits: bit 60 down to 0)
        for (int bit = 60; bit >= 0; bit--) {
            if ((r.data >> bit) & 1) {
                pulses.push_back({(int32_t)t.te_long});
            } else {
                pulses.push_back({(int32_t)t.te_short});
            }
            pulses.push_back({-(int32_t)t.te_short});
        }

        pulses.push_back({-2000});
        return pulses;
    }

private:
    enum {
        StepReset = 0,
        StepPreamble,
        StepSaveDuration,
        StepCheckDuration,
    };

    static bool isShort(uint32_t d) { return DURATION_DIFF(d, 250) < 100; }
    static bool isLong(uint32_t d)  { return DURATION_DIFF(d, 500) < 100; }
    static bool isKiaGap(uint32_t d) { return d >= 700 && (d - 700) <= 1000; }  // 700..1700

    /// CRC-8 poly 0x7F, init 0 (upstream crc8_kia / common crc8).
    static uint8_t crc8kia(const uint8_t* b, int n) {
        uint8_t c = 0;
        for (int i = 0; i < n; i++) {
            c ^= b[i];
            for (int j = 0; j < 8; j++)
                c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x7F) : (uint8_t)(c << 1);
        }
        return c;
    }
    /// Upstream kia_v0_verify_crc_poly: CRC over frame bytes 6..1 (bits 55..8).
    static bool verifyCrcPoly(uint64_t data) {
        uint8_t bs[6] = {
            (uint8_t)(data >> 48), (uint8_t)(data >> 40), (uint8_t)(data >> 32),
            (uint8_t)(data >> 24), (uint8_t)(data >> 16), (uint8_t)(data >> 8)};
        return crc8kia(bs, 6) == (uint8_t)(data & 0xFF);
    }

    bool extractData() {
        result_.data = decodeData_;
        result_.dataBits = decodeCountBit_;
        result_.protocolName = PROTOCOL_NAME;

        // Bit layout (61 bits):
        // [60..57] reserved, [55..40] counter, [39..12] serial, [11..8] button, [7..0] CRC
        result_.counter = (decodeData_ >> 40) & 0xFFFF;
        result_.serial  = (decodeData_ >> 12) & 0x0FFFFFFF;
        result_.button  = (decodeData_ >> 8) & 0x0F;
        result_.crc     = decodeData_ & 0xFF;

        result_.crcValid = verifyCrcPoly(decodeData_);
        result_.canEmulate = true;
        result_.encrypted = false;

        parserStep_ = StepReset;
        return true;
    }
};

PP_REGISTER_PROTOCOL(PPKiaV0)
