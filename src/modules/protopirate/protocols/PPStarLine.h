#pragma once
/**
 * @file PPStarLine.h
 * @brief StarLine key fob protocol decoder/encoder.
 *
 * PWM: te_short=250µs, te_long=500µs, 64-bit.
 * KeeLoq encrypted with keystore (Simple/Normal learning).
 * Ported from ProtoPirate star_line.c
 */

#include "PPProtocol.h"
#include "PPCommon.h"
#include "KeeloqCipher.h"
#include "PPKeystore.h"

class PPStarLine : public PPProtocol {
public:
    static constexpr const char* PROTOCOL_NAME = "StarLine";

    const char* getName() const override { return PROTOCOL_NAME; }
    bool canEmulate() const override { return true; }

    // ---- Forward-code generation (authorized testing) ----
    bool supportsForward() const override { return true; }

    /// Advance the KeeLoq counter by `delta` and re-encrypt with the same
    /// device key that decoded the capture. Requires the manufacturer key to
    /// be present in the keystore (same as decode).
    bool generateForward(const PPDecodeResult& in, int32_t delta,
                         PPDecodeResult& out) override {
        uint64_t key    = pp_reverse64(in.data);
        uint32_t keyFix = (uint32_t)(key >> 32);
        uint32_t keyHop = (uint32_t)(key & 0xFFFFFFFF);
        uint8_t  btn    = (uint8_t)(keyFix >> 24);
        uint16_t endSer = (uint16_t)(keyFix & 0xFF);

        const char* mf = "?";
        uint32_t cnt = 0;
        uint64_t deviceKey = 0;
        if (!matchKey(keyFix, keyHop, btn, endSer, mf, cnt, deviceKey)) {
            return false;  // no key in store → cannot roll forward
        }

        uint32_t newCnt = (cnt + (uint32_t)delta) & 0xFFFF;
        // Plaintext layout: [btn:8][endSerial:8][counter:16]
        uint32_t plain  = ((uint32_t)btn << 24) |
                          ((uint32_t)(endSer & 0xFF) << 16) | newCnt;
        uint32_t newHop = keeloq::encrypt(plain, deviceKey);
        uint64_t newKey = ((uint64_t)keyFix << 32) | newHop;

        out = in;
        out.data       = pp_reverse64(newKey);
        out.counter    = newCnt;
        out.canEmulate = true;
        out.type       = mf;
        return true;
    }

    const PPTimingConst& getTiming() const override {
        static const PPTimingConst t = {250, 500, 120, 64};
        return t;
    }

    void reset() override {
        resetDecoder();
    }

    /**
     * @brief Set a manufacturer key for KeeLoq decryption.
     * @param index Key index (0-based)
     * @param name  Manufacturer name
     * @param key   64-bit KeeLoq key
     * @param type  Learning type (0=Simple, 1=Normal, 2=Unknown)
     */
    // Compatibility shims → the shared central keystore (pp::Keystore).
    static void addManufacturerKey(size_t /*index*/, const char* name,
                                   uint64_t key, uint8_t type) {
        pp::Keystore::add(name, key, type);
    }

    static void clearKeys() { pp::Keystore::clear(); }

    bool feed(bool level, uint32_t duration) override {
        auto& t = getTiming();

        switch (parserStep_) {
        case StepReset:
            if (level) {
                if (DURATION_DIFF(duration, t.te_long * 2) <
                    t.te_delta * 2) {
                    parserStep_ = StepCheckPreamble;
                    headerCount_++;
                } else if (headerCount_ > 4) {
                    decodeData_ = 0;
                    decodeCountBit_ = 0;
                    teLast_ = duration;
                    parserStep_ = StepCheckDuration;
                }
            } else {
                headerCount_ = 0;
            }
            break;

        case StepCheckPreamble:
            if (!level &&
                DURATION_DIFF(duration, t.te_long * 2) < t.te_delta * 2) {
                // Preamble pair found
                parserStep_ = StepReset;
            } else {
                headerCount_ = 0;
                parserStep_ = StepReset;
            }
            break;

        case StepSaveDuration:
            if (level) {
                if (duration >= (t.te_long + t.te_delta)) {
                    // End of data
                    parserStep_ = StepReset;
                    if (decodeCountBit_ >= t.min_count_bit &&
                        decodeCountBit_ <= t.min_count_bit + 2) {
                        if (lastData_ != decodeData_) {
                            lastData_ = decodeData_;
                            return extractData();
                        }
                    }
                    decodeData_ = 0;
                    decodeCountBit_ = 0;
                    headerCount_ = 0;
                } else {
                    teLast_ = duration;
                    parserStep_ = StepCheckDuration;
                }
            } else {
                parserStep_ = StepReset;
            }
            break;

        case StepCheckDuration:
            if (!level) {
                if (DURATION_DIFF(teLast_, t.te_short) < t.te_delta &&
                    DURATION_DIFF(duration, t.te_short) < t.te_delta) {
                    // Bit 0
                    if (decodeCountBit_ < t.min_count_bit) {
                        addBit(0);
                    } else {
                        decodeCountBit_++;
                    }
                    parserStep_ = StepSaveDuration;
                } else if (DURATION_DIFF(teLast_, t.te_long) < t.te_delta &&
                           DURATION_DIFF(duration, t.te_long) < t.te_delta) {
                    // Bit 1
                    if (decodeCountBit_ < t.min_count_bit) {
                        addBit(1);
                    } else {
                        decodeCountBit_++;
                    }
                    parserStep_ = StepSaveDuration;
                } else {
                    parserStep_ = StepReset;
                }
            } else {
                parserStep_ = StepReset;
            }
            break;
        }
        return false;
    }

    std::vector<PPPulse> generatePulseData(const PPDecodeResult& r) override {
        std::vector<PPPulse> pulses;
        auto& t = getTiming();

        // 6 preamble long-pulse pairs
        for (int i = 0; i < 6; i++) {
            pulses.push_back({(int32_t)(t.te_long * 2)});
            pulses.push_back({-(int32_t)(t.te_long * 2)});
        }

        // Data bits (MSB first)
        for (int i = 63; i >= 0; i--) {
            if ((r.data >> i) & 1) {
                pulses.push_back({(int32_t)t.te_long});
                pulses.push_back({-(int32_t)t.te_long});
            } else {
                pulses.push_back({(int32_t)t.te_short});
                pulses.push_back({-(int32_t)t.te_short});
            }
        }

        return pulses;
    }

private:
    uint64_t lastData_ = 0;

    enum {
        StepReset = 0,
        StepCheckPreamble,
        StepSaveDuration,
        StepCheckDuration,
    };

    void addBit(uint8_t b) {
        decodeData_ = (decodeData_ << 1) | b;
        decodeCountBit_++;
    }

    bool extractData() {
        // Reverse key bits (original uses subghz_protocol_blocks_reverse_key)
        uint64_t key = pp_reverse64(decodeData_);
        uint32_t keyFix = (uint32_t)(key >> 32);
        uint32_t keyHop = (uint32_t)(key & 0xFFFFFFFF);

        uint32_t serial = keyFix & 0x00FFFFFF;
        uint8_t btn = (uint8_t)(keyFix >> 24);
        uint16_t endSerial = (uint16_t)(keyFix & 0xFF);

        // Try all manufacturer keys (shared keystore)
        const char* mfName = "Unknown";
        uint32_t cnt = 0;
        uint64_t deviceKey = 0;
        bool found = matchKey(keyFix, keyHop, btn, endSerial, mfName, cnt, deviceKey);

        result_.data = decodeData_;
        result_.dataBits = 64;
        result_.protocolName = PROTOCOL_NAME;
        result_.serial = serial;
        result_.button = btn;
        result_.counter = cnt;
        result_.encrypted = true;
        result_.canEmulate = found;
        result_.type = mfName;

        return true;
    }

    /// Find the shared-keystore key that decrypts this hop. Outputs the
    /// recovered counter AND the device key actually used (post-learning),
    /// so generateForward() can re-encrypt the advanced counter identically.
    /// Learning codes follow keeloq::LearningType (1=simple, 2=normal, else=unknown).
    bool matchKey(uint32_t fix, uint32_t hop, uint8_t btn, uint16_t endSerial,
                  const char*& mfName, uint32_t& cnt, uint64_t& deviceKeyOut) {
        for (size_t i = 0; i < pp::Keystore::count(); i++) {
            const pp::MfKey* mk = pp::Keystore::get(i);
            auto tryKey = [&](uint64_t dk) -> bool {
                uint32_t d = keeloq::decrypt(hop, dk);
                if (checkDecrypt(d, btn, endSerial, cnt)) {
                    deviceKeyOut = dk;
                    mfName = mk->name;
                    return true;
                }
                return false;
            };
            // Byte-mirrored manufacturer key (some stores list keys reversed)
            uint64_t rev = 0;
            for (uint8_t j = 0; j < 64; j += 8)
                rev |= (uint64_t)((uint8_t)(mk->key >> j)) << (56 - j);

            switch (mk->learning) {
            case 1:  // SIMPLE: manufacturer key is the device key
                if (tryKey(mk->key)) return true;
                break;
            case 2:  // NORMAL: derive device key from serial
                if (tryKey(keeloq::normalLearning(fix, mk->key))) return true;
                break;
            default: // UNKNOWN: try simple, mirrored, normal, mirrored-normal
                if (tryKey(mk->key)) return true;
                if (tryKey(rev)) return true;
                if (tryKey(keeloq::normalLearning(fix, mk->key))) return true;
                if (tryKey(keeloq::normalLearning(fix, rev))) return true;
                break;
            }
        }
        return false;
    }

    static bool checkDecrypt(uint32_t decrypt, uint8_t btn,
                             uint16_t endSerial, uint32_t& cnt) {
        if ((decrypt >> 24 == btn) &&
            (((uint16_t)(decrypt >> 16) & 0x00FF) == endSerial)) {
            cnt = decrypt & 0x0000FFFF;
            return true;
        }
        return false;
    }
};

PP_REGISTER_PROTOCOL(PPStarLine)
