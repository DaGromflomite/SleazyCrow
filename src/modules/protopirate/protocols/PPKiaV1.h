#pragma once
/**
 * @file PPKiaV1.h
 * @brief Kia V1 key fob protocol decoder/encoder.
 *
 * Manchester encoding: te_short=800µs, te_long=1600µs, 57-bit, CRC4.
 * Ported from ProtoPirate kia_v1.c
 */

#include "PPProtocol.h"

class PPKiaV1 : public PPProtocol {
public:
    static constexpr const char* PROTOCOL_NAME = "Kia V1";

    const char* getName() const override { return PROTOCOL_NAME; }
    bool canEmulate() const override { return true; }

    const PPTimingConst& getTiming() const override {
        static const PPTimingConst t = {800, 1600, 200, 56};
        return t;
    }

    void reset() override {
        resetDecoder();
        manchState_ = MS_Mid1;
    }

    // Faithful port of kia_protocol_decoder_v1_feed (upstream RocketGod/ProtoPirate):
    // te_long preamble pairs (tracked on LOWs), header_count>70 then a short LOW
    // seeds a leading 1, then STANDARD Flipper Manchester to 57 bits. (Our previous
    // feed used a divergent half-bit tracker that never decoded real fobs.)
    bool feed(bool level, uint32_t duration) override {
        const uint32_t te_short = 800, te_long = 1600, te_delta = 200;

        switch (parserStep_) {
        case StepReset:
            if (level && DURATION_DIFF(duration, te_long) < te_delta) {
                parserStep_ = StepCheckPreamble;
                teLast_ = duration;
                headerCount_ = 0;
                decodeData_ = 0;
                decodeCountBit_ = 0;
                manchState_ = MS_Mid1;   // manchester_advance(Reset) -> Mid1
            }
            break;

        case StepCheckPreamble:
            // Two independent blocks (NOT else-if): the second can override the
            // first's reset on the short LOW that ends the preamble. (Upstream.)
            if (!level) {
                if (DURATION_DIFF(duration, te_long) < te_delta &&
                    DURATION_DIFF(teLast_, te_long) < te_delta) {
                    headerCount_++;
                    teLast_ = duration;
                } else {
                    parserStep_ = StepReset;
                }
            }
            if (headerCount_ > 70) {
                if (!level &&
                    DURATION_DIFF(duration, te_short) < te_delta &&
                    DURATION_DIFF(teLast_, te_long) < te_delta) {
                    decodeData_ = 0;
                    decodeCountBit_ = 0;
                    addBit(1);                 // leading 1
                    headerCount_ = 0;
                    parserStep_ = StepDecodeData;
                }
            }
            break;

        case StepDecodeData: {
            int ev = manchEvent(duration, level, te_short, te_long, te_delta);
            if (ev != ME_Reset) {
                bool bit;
                if (manchAdvance(ev, bit)) {
                    addBit(bit ? 1 : 0);
                }
            }
            // 56-bit payload (leading 1 + 55 Manchester bits). Verified against
            // the real KAT capture KIA V1 N1 RAW -> sn=B444706E btn=1 cnt=0x91.
            if (decodeCountBit_ == 56) {
                return extractData();
            }
            break;
        }
        }
        return false;
    }

    std::vector<PPPulse> generatePulseData(const PPDecodeResult& r) override {
        std::vector<PPPulse> pulses;
        auto& t = getTiming();

        // Preamble: 90 long HIGH/LOW pairs
        for (int i = 0; i < 90; i++) {
            pulses.push_back({(int32_t)t.te_long});
            pulses.push_back({-(int32_t)t.te_long});
        }

        // Manchester encode data bits
        bool prev = true;
        for (int bit = 56; bit >= 0; bit--) {
            bool val = (r.data >> bit) & 1;
            if (val == prev) {
                // Same: short-short
                pulses.push_back({(int32_t)t.te_short});
                pulses.push_back({-(int32_t)t.te_short});
            } else {
                // Different: long
                pulses.push_back({val ? (int32_t)t.te_long : -(int32_t)t.te_long});
            }
            prev = val;
        }

        pulses.push_back({-3000});
        return pulses;
    }

private:
    enum {
        StepReset = 0,
        StepCheckPreamble,
        StepDecodeData,
    };

    // ---- Standard Flipper Manchester (same as the shared lib upstream uses) ----
    enum { ME_ShortLow = 0, ME_ShortHigh = 2, ME_LongLow = 4,
           ME_LongHigh = 6, ME_Reset = 8 };
    enum { MS_Start1 = 0, MS_Mid1 = 1, MS_Mid0 = 2, MS_Start0 = 3 };
    uint8_t manchState_ = MS_Mid1;

    static int manchEvent(uint32_t dur, bool level, uint32_t te_short,
                          uint32_t te_long, uint32_t te_delta) {
        if (DURATION_DIFF(dur, te_short) < te_delta)
            return level ? ME_ShortLow : ME_ShortHigh;
        if (DURATION_DIFF(dur, te_long) < te_delta)
            return level ? ME_LongLow : ME_LongHigh;
        return ME_Reset;
    }
    bool manchAdvance(int event, bool& bit) {
        static const uint8_t transitions[4] = {0x01, 0x91, 0x9B, 0xFB};
        if (event == ME_Reset) { manchState_ = MS_Mid1; return false; }
        uint8_t ns = (uint8_t)((transitions[manchState_] >> event) & 0x3);
        if (ns == manchState_) { manchState_ = MS_Mid1; return false; }
        bool result = false;
        if (ns == MS_Mid0) { bit = false; result = true; }
        else if (ns == MS_Mid1) { bit = true; result = true; }
        manchState_ = ns;
        return result;
    }

    /// CRC4: XOR of all nibbles of the 7 field bytes, + offset. (Upstream.)
    static uint8_t crc4(const uint8_t* bytes, int count, uint8_t offset) {
        uint8_t crc = 0;
        for (int i = 0; i < count; i++) crc ^= (bytes[i] & 0x0F) ^ (bytes[i] >> 4);
        return (uint8_t)((crc + offset) & 0x0F);
    }

    bool extractData() {
        uint64_t data = decodeData_;
        result_.data = data;
        result_.dataBits = (uint16_t)decodeCountBit_;
        result_.protocolName = PROTOCOL_NAME;

        // Field layout (upstream kia_v1_check_remote_controller):
        uint32_t serial = (uint32_t)(data >> 24);
        uint8_t  btn    = (uint8_t)((data >> 16) & 0xFF);
        uint16_t cnt    = (uint16_t)((((data >> 4) & 0xF) << 8) | ((data >> 8) & 0xFF));
        result_.serial  = serial;
        result_.button  = btn;
        result_.counter = cnt;
        result_.crc     = (uint8_t)(data & 0x0F);

        uint8_t cntHigh = (uint8_t)((cnt >> 8) & 0xF);
        uint8_t cd[7] = {
            (uint8_t)((serial >> 24) & 0xFF), (uint8_t)((serial >> 16) & 0xFF),
            (uint8_t)((serial >> 8) & 0xFF),  (uint8_t)(serial & 0xFF),
            btn, (uint8_t)(cnt & 0xFF), cntHigh };
        result_.crcValid = (crc4(cd, 7, 1) == (uint8_t)(data & 0x0F));
        result_.canEmulate = true;
        result_.encrypted = false;

        parserStep_ = StepReset;
        return true;
    }
};

PP_REGISTER_PROTOCOL(PPKiaV1)
