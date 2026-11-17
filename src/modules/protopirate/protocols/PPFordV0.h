#pragma once
/**
 * @file PPFordV0.h
 * @brief Ford V0 key fob protocol decoder/encoder.
 *
 * Differential Manchester: te_short=250µs, te_long=500µs, 64-bit.
 * GF(2) matrix CRC, BS calculation, XOR obfuscation.
 * Ported from ProtoPirate ford_v0.c
 */

#include "PPProtocol.h"

class PPFordV0 : public PPProtocol {
public:
    static constexpr const char* PROTOCOL_NAME = "Ford V0";

    const char* getName() const override { return PROTOCOL_NAME; }
    bool canEmulate() const override { return true; }

    const PPTimingConst& getTiming() const override {
        static const PPTimingConst t = {250, 500, 100, 64};
        return t;
    }

    void reset() override {
        resetDecoder();
        key1_ = 0;
        key2_ = 0;
        dataLow_ = 0;
        dataHigh_ = 0;
        bitCount_ = 0;
        manchState_ = MS_Mid1;
    }

    // Faithful port of subghz_protocol_decoder_ford_v0_feed (standard Flipper
    // Manchester). Our previous feed was a divergent, broken port (it never
    // round-tripped the real encoder, so Ford decode was effectively broken).
    bool feed(bool level, uint32_t duration) override {
        const uint32_t te_short = 250, te_long = 500, te_delta = 100, gap = 3500;

        switch (parserStep_) {
        case StepReset:
            if (level && DURATION_DIFF(duration, te_short) < te_delta) {
                dataLow_ = 0; dataHigh_ = 0; bitCount_ = 0; headerCount_ = 0;
                teLast_ = duration;
                manchState_ = MS_Mid1;
                parserStep_ = StepPreamble;
            }
            break;

        case StepPreamble:
            if (!level) {
                if (DURATION_DIFF(duration, te_long) < te_delta) {
                    teLast_ = duration; parserStep_ = StepPreambleCheck;
                } else parserStep_ = StepReset;
            }
            break;

        case StepPreambleCheck:
            if (level) {
                if (DURATION_DIFF(duration, te_long) < te_delta) {
                    headerCount_++; teLast_ = duration; parserStep_ = StepPreamble;
                } else if (DURATION_DIFF(duration, te_short) < te_delta) {
                    parserStep_ = StepGap;
                } else parserStep_ = StepReset;
            }
            break;

        case StepGap:
            if (!level && DURATION_DIFF(duration, gap) < 250) {
                dataLow_ = 1; dataHigh_ = 0; bitCount_ = 1;
                parserStep_ = StepData;
            } else if (!level && duration > gap + 250) {
                parserStep_ = StepReset;
            }
            break;

        case StepData: {
            int ev = manchEvent(duration, level, te_short, te_long, te_delta);
            if (ev == ME_Reset) { parserStep_ = StepReset; break; }
            bool bit;
            if (manchAdvance(ev, bit)) {
                addBit(bit);
                if (processData()) { parserStep_ = StepReset; return true; }
            }
            teLast_ = duration;
            break;
        }
        }
        return false;
    }

    // Faithful port of the upstream ProtoPirate ford_v0 encoder
    // (subghz_protocol_encoder_ford_v0_get_upload): the key is transmitted
    // INVERTED (~key1/~key2), after a lead-in + 4 preamble pairs + a sync gap,
    // in a specific differential-Manchester convention. One burst is emitted;
    // emulate() repeats it.
    std::vector<PPPulse> generatePulseData(const PPDecodeResult& r) override {
        std::vector<PPPulse> p;
        auto& t = getTiming();                 // te_short=250, te_long=500
        const int32_t S = (int32_t)t.te_short;
        const int32_t L = (int32_t)t.te_long;

        uint64_t txk1 = ~r.data;               // transmit the inverse
        uint16_t txk2 = (uint16_t)(~(uint16_t)r.data2);

        bool prev = false;
        auto emit = [&](bool curr) {
            if (!prev && !curr)     { p.push_back({ S}); p.push_back({-S}); }
            else if (!prev && curr) { p.push_back({ L}); }
            else if (prev && !curr) { p.push_back({-L}); }
            else                    { p.push_back({-S}); p.push_back({ S}); }
            prev = curr;
        };

        // Lead-in + 4 preamble pairs + sync gap
        p.push_back({ S}); p.push_back({-L});
        for (int i = 0; i < 4; i++) { p.push_back({ L}); p.push_back({-L}); }
        p.push_back({ S}); p.push_back({-3500});

        // First key1 bit (62) has a special lead-in
        bool first = (txk1 >> 62) & 1;
        if (first) { p.push_back({ L}); }
        else       { p.push_back({ S}); p.push_back({-L}); }
        prev = first;
        for (int bit = 61; bit >= 0; --bit) emit((txk1 >> bit) & 1);
        for (int bit = 15; bit >= 0; --bit) emit((txk2 >> bit) & 1);

        p.push_back({-4000});
        return p;
    }

    // ---- Forward-code generation (authorized testing) ----
    // Ford V0 is unencrypted: a reversible obfuscation (XOR + a symmetric
    // byte-swap) over a 20-bit counter, with a GF(2) CRC. The counter is
    // advanced in the deobfuscated frame, then the obfuscation and CRC are
    // re-applied (both steps are their own inverse).
    bool supportsForward() const override { return true; }
    bool generateForward(const PPDecodeResult& in, int32_t delta,
                         PPDecodeResult& out) override {
        uint64_t key1 = in.data;
        uint16_t key2 = (uint16_t)in.data2;

        uint8_t buf[13] = {0};
        for (int i = 0; i < 8; ++i) buf[i] = (uint8_t)(key1 >> (56 - i * 8));
        buf[8] = (uint8_t)(key2 >> 8);

        // buf[11] = parity of the BS byte (buf[8], unchanged by forwarding)
        uint8_t tmp = buf[8], parity = 0, parity_any = (buf[8] != 0);
        while (tmp) { parity ^= (tmp & 1); tmp >>= 1; }
        buf[11] = parity_any ? parity : 0;

        // Deobfuscate exactly as decodeFord → buf[5..7] hold button+counter.
        uint8_t xor_byte; int limit;
        if (buf[11]) { xor_byte = buf[7]; limit = 7; }
        else         { xor_byte = buf[6]; limit = 6; }
        for (int idx = 1; idx < limit; ++idx) buf[idx] ^= xor_byte;
        if (buf[11] == 0) buf[7] ^= xor_byte;
        uint8_t ob7 = buf[7];
        buf[7] = (ob7 & 0xAA) | (buf[6] & 0x55);
        buf[6] = (buf[6] & 0xAA) | (ob7 & 0x55);

        // Advance the 19-bit counter, keep the button field (buf[5] bits 6..3).
        uint8_t button = (buf[5] >> 3) & 0x0F;
        uint32_t newCnt = (in.counter + (uint32_t)delta) & 0x7FFFF;
        buf[5] = (uint8_t)(((button & 0x0F) << 3) | ((newCnt >> 16) & 0x07));
        buf[6] = (uint8_t)((newCnt >> 8) & 0xFF);
        buf[7] = (uint8_t)(newCnt & 0xFF);

        // Re-obfuscate: undo the swap (symmetric), then re-apply the XOR.
        uint8_t d6 = buf[6], d7 = buf[7];
        buf[7] = (d7 & 0xAA) | (d6 & 0x55);
        buf[6] = (d6 & 0xAA) | (d7 & 0x55);
        uint8_t xb;
        if (buf[11]) { xb = buf[7]; for (int i = 1; i < 7; ++i) buf[i] ^= xb; }
        else { xb = buf[6]; for (int i = 1; i < 6; ++i) buf[i] ^= xb; buf[7] ^= xb; }

        uint64_t newKey1 = 0;
        for (int i = 0; i < 8; ++i) newKey1 |= (uint64_t)buf[i] << (56 - i * 8);

        // Recompute CRC (received byte = computed ^ 0x80), BS byte unchanged.
        uint8_t cbuf[16] = {0};
        for (int i = 0; i < 8; ++i) cbuf[i] = (uint8_t)(newKey1 >> (56 - i * 8));
        cbuf[8] = (uint8_t)(key2 >> 8);
        uint8_t crc = (uint8_t)(computeCrc(cbuf) ^ 0x80);
        uint16_t newKey2 = (uint16_t)(((key2 >> 8) << 8) | crc);

        out = in;
        out.data = newKey1;
        out.data2 = newKey2;
        out.counter = newCnt;
        out.canEmulate = true;
        return true;
    }

private:
    uint64_t key1_ = 0;
    uint16_t key2_ = 0;
    uint32_t dataLow_ = 0;
    uint32_t dataHigh_ = 0;
    uint16_t bitCount_ = 0;

    enum { StepReset = 0, StepPreamble, StepPreambleCheck, StepGap, StepData };

    // Standard Flipper Manchester decoder (lib/toolbox/manchester_decoder).
    enum { ME_ShortLow = 0, ME_ShortHigh = 2, ME_LongLow = 4,
           ME_LongHigh = 6, ME_Reset = 8 };
    enum { MS_Start1 = 0, MS_Mid1 = 1, MS_Mid0 = 2, MS_Start0 = 3 };
    uint8_t manchState_ = MS_Mid1;

    static int manchEvent(uint32_t dur, bool level, uint32_t te_short,
                          uint32_t te_long, uint32_t te_delta) {
        // Matches upstream pp_manchester_event (protocols_common.h).
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

    void addBit(bool bit) {
        uint32_t low = dataLow_;
        dataLow_ = (dataLow_ << 1) | (bit ? 1u : 0u);
        dataHigh_ = (dataHigh_ << 1) | ((low >> 31) & 1u);
        bitCount_++;
    }

    // Returns true when a full 80-bit frame is decoded (key1=~, key2=~).
    bool processData() {
        if (bitCount_ == 64) {
            uint64_t combined = ((uint64_t)dataHigh_ << 32) | dataLow_;
            key1_ = ~combined;
            dataLow_ = 0; dataHigh_ = 0;
            return false;
        }
        if (bitCount_ == 80) {
            key2_ = (uint16_t)(~(uint16_t)(dataLow_ & 0xFFFF));
            uint32_t serial, counter; uint8_t button; bool crcValid;
            decodeFields(key1_, key2_, serial, button, counter, crcValid);
            result_.data = key1_;
            result_.data2 = key2_;
            result_.dataBits = 64;
            result_.protocolName = PROTOCOL_NAME;
            result_.serial = serial;
            result_.button = button;
            result_.counter = counter;
            result_.crc = (uint8_t)(key2_ & 0xFF);
            result_.crcValid = crcValid;
            result_.canEmulate = true;
            result_.encrypted = false;
            return true;
        }
        return false;
    }

    /// GF(2) CRC matrix (64 bytes, exact values from original ford_v0.c)
    static constexpr uint8_t CRC_MATRIX[64] = {
        0xDA, 0xB5, 0x55, 0x6A, 0xAA, 0xAA, 0xAA, 0xD5,
        0xB6, 0x6C, 0xCC, 0xD9, 0x99, 0x99, 0x99, 0xB3,
        0x71, 0xE3, 0xC3, 0xC7, 0x87, 0x87, 0x87, 0x8F,
        0x0F, 0xE0, 0x3F, 0xC0, 0x7F, 0x80, 0x7F, 0x80,
        0x00, 0x1F, 0xFF, 0xC0, 0x00, 0x7F, 0xFF, 0x80,
        0x00, 0x00, 0x00, 0x3F, 0xFF, 0xFF, 0xFF, 0x80,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F,
        0x23, 0x12, 0x94, 0x84, 0x35, 0xF4, 0x55, 0x84,
    };

    static uint8_t popcount8(uint8_t x) {
        uint8_t count = 0;
        while (x) { count += x & 1; x >>= 1; }
        return count;
    }

    /// Compute GF(2) matrix CRC (exact port of ford_v0_calculate_crc)
    /// @param buf  buffer; CRC is computed over buf[1..8]
    static uint8_t computeCrc(const uint8_t* buf) {
        uint8_t crc = 0;
        for (int row = 0; row < 8; row++) {
            uint8_t xor_sum = 0;
            for (int col = 0; col < 8; col++) {
                xor_sum ^= (CRC_MATRIX[row * 8 + col] & buf[col + 1]);
            }
            if (popcount8(xor_sum) & 1) {
                crc |= (1 << row);
            }
        }
        return crc;
    }

    /// Verify CRC (exact port of ford_v0_verify_crc)
    static bool verifyCrc(uint64_t key1, uint16_t key2) {
        uint8_t buf[16] = {0};
        for (int i = 0; i < 8; ++i) {
            buf[i] = (uint8_t)(key1 >> (56 - i * 8));
        }
        buf[8] = (uint8_t)(key2 >> 8);

        uint8_t calculated = computeCrc(buf);
        uint8_t received   = (uint8_t)(key2 & 0xFF) ^ 0x80;
        return (calculated == received);
    }

public:
    /// Decode Ford fields directly from key1/key2 (data-level, no RF). Used by
    /// decodeFord() and by the forward-code self-test to verify the obfuscation
    /// and CRC math without the Manchester encoder.
    static bool decodeFields(uint64_t key1, uint16_t key2, uint32_t& serial,
                             uint8_t& button, uint32_t& counter, bool& crcValid) {
        uint8_t buf[13] = {0};
        for (int i = 0; i < 8; ++i) buf[i] = (uint8_t)(key1 >> (56 - i * 8));
        buf[8] = (uint8_t)(key2 >> 8);
        buf[9] = (uint8_t)(key2 & 0xFF);

        uint8_t tmp = buf[8], parity = 0, parity_any = (buf[8] != 0);
        while (tmp) { parity ^= (tmp & 1); tmp >>= 1; }
        buf[11] = parity_any ? parity : 0;

        uint8_t xor_byte; int limit;
        if (buf[11]) { xor_byte = buf[7]; limit = 7; }
        else         { xor_byte = buf[6]; limit = 6; }
        for (int idx = 1; idx < limit; ++idx) buf[idx] ^= xor_byte;
        if (buf[11] == 0) buf[7] ^= xor_byte;
        uint8_t ob7 = buf[7];
        buf[7] = (ob7 & 0xAA) | (buf[6] & 0x55);
        buf[6] = (buf[6] & 0xAA) | (ob7 & 0x55);

        uint32_t sle = ((uint32_t)buf[1]) | ((uint32_t)buf[2] << 8) |
                       ((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 24);
        serial = ((sle & 0xFF) << 24) | (((sle >> 8) & 0xFF) << 16) |
                 (((sle >> 16) & 0xFF) << 8) | ((sle >> 24) & 0xFF);
        button = (buf[5] >> 3) & 0x0F;
        counter = ((uint32_t)(buf[5] & 0x07) << 16) |
                  ((uint32_t)buf[6] << 8) | buf[7];
        crcValid = verifyCrc(key1, key2);
        return true;
    }

private:
    /// Decode Ford V0 (exact port of decode_ford_v0)
    bool decodeFord() {
        uint8_t buf[13] = {0};

        // Unpack key1 big-endian (NO inversion)
        for (int i = 0; i < 8; ++i) {
            buf[i] = (uint8_t)(key1_ >> (56 - i * 8));
        }
        buf[8] = (uint8_t)(key2_ >> 8);   // BS byte
        buf[9] = (uint8_t)(key2_ & 0xFF); // CRC byte

        // BS parity calculation
        uint8_t tmp = buf[8];
        uint8_t bs  = tmp;
        uint8_t parity = 0;
        uint8_t parity_any = (tmp != 0);
        while (tmp) {
            parity ^= (tmp & 1);
            tmp >>= 1;
        }
        buf[11] = parity_any ? parity : 0;

        // XOR deobfuscation (conditional byte and limit)
        uint8_t xor_byte;
        uint8_t limit;
        if (buf[11]) {
            xor_byte = buf[7];
            limit = 7;
        } else {
            xor_byte = buf[6];
            limit = 6;
        }

        for (int idx = 1; idx < limit; ++idx) {
            buf[idx] ^= xor_byte;
        }

        if (buf[11] == 0) {
            buf[7] ^= xor_byte;
        }

        // Mixed byte swap (buf[6] ↔ buf[7] interleaved)
        uint8_t orig_b7 = buf[7];
        buf[7]  = (orig_b7 & 0xAA)  | (buf[6] & 0x55);
        uint8_t mixed = (buf[6] & 0xAA) | (orig_b7 & 0x55);
        buf[12] = mixed;
        buf[6]  = mixed;

        // Serial: 32-bit from buf[1..4] (LE read → byte-swap to BE)
        uint32_t serial_le = ((uint32_t)buf[1]) |
                             ((uint32_t)buf[2] << 8) |
                             ((uint32_t)buf[3] << 16) |
                             ((uint32_t)buf[4] << 24);
        uint32_t serial = ((serial_le & 0xFF) << 24) |
                          (((serial_le >> 8)  & 0xFF) << 16) |
                          (((serial_le >> 16) & 0xFF) << 8) |
                          ((serial_le >> 24) & 0xFF);

        // Button: buf[5] bits 6..3 (matches upstream decode_ford_v0)
        uint8_t button = (buf[5] >> 3) & 0x0F;

        // Counter: 19-bit from buf[5] bits 2..0 + buf[6..7]
        uint32_t counter = ((uint32_t)(buf[5] & 0x07) << 16) |
                           ((uint32_t)buf[6] << 8) |
                           buf[7];

        // BS magic
        uint8_t bsMagic = bs + ((bs & 0x80) ? 0x80 : 0) -
                          (button << 4) - (uint8_t)(counter & 0xFF);

        // CRC verification
        bool crcValid = verifyCrc(key1_, key2_);

        result_.data = key1_;
        result_.data2 = key2_;
        result_.dataBits = 64;
        result_.protocolName = PROTOCOL_NAME;
        result_.serial = serial;
        result_.button = button;
        result_.counter = counter;
        result_.bsMagic = bsMagic;
        result_.crc = (uint8_t)(key2_ & 0xFF);
        result_.crcValid = crcValid;
        result_.canEmulate = true;
        result_.encrypted = false;

        parserStep_ = StepReset;
        return true;
    }
};

PP_REGISTER_PROTOCOL(PPFordV0)
