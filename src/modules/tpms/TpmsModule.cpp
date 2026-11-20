#include "modules/tpms/TpmsModule.h"

#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "modules/CC1101_driver/CC1101_Worker.h"
#include "modules/tpms/TpmsDecoders.h"
#include "core/ble/ClientsManager.h"
#include "BinaryMessages.h"

// Decode a Manchester bit array ANCHORED to its preamble, rather than brute-
// forcing every bit offset. These TPMS frames are an alternating preamble (which
// Manchester-decodes to a long run of one constant bit) + a sync bit that ends
// the run + the payload. So the payload starts at (preamble-run length)+1. Trying
// every offset instead let an 8-bit checksum collide at a shifted alignment and
// surface a deterministic phantom sensor (found on real Ford frames; worse as
// more decoders were added). Anchoring tries ONE offset per inversion.
//
// `cleanLen` = number of leading bits that came from unbroken Manchester pairs;
// the frame must lie inside it. Of the (inversion) candidates, the genuine frame
// has the LONGEST preamble lock, so we keep the max. Returns that run length
// (0 = nothing decoded), writing the decode into `out`. Verified ghost-free and
// miss-free over 16k synthetic frames + 8k noise segments in a faithful model of
// this exact demod.
static int anchorDecodeBits(const uint8_t* bits, int nb, int cleanLen,
                            TpmsDecoded& out, int family) {
    int bestL = 0;
    TpmsDecoded best;
    uint8_t bytes[12];
    for (int inv = 0; inv < 2; inv++) {
        uint8_t first = (uint8_t)(bits[0] ^ inv);
        int L = 1;
        while (L < nb && (uint8_t)(bits[L] ^ inv) == first) L++;
        if (L < 12) continue;                 // need a real, long preamble run
        int off = L + 1;                       // payload starts after the sync bit
        if (off + 64 > nb || off > cleanLen) continue;
        int maxbytes = (nb - off) / 8;
        if (maxbytes > 12) maxbytes = 12;
        for (int B = 0; B < maxbytes; B++) {
            uint8_t v = 0;
            for (int b = 0; b < 8; b++)
                v = (uint8_t)((v << 1) | ((bits[off + B * 8 + b] ^ inv) & 1));
            bytes[B] = v;
        }
        TpmsDecoded cur;
        if (maxbytes >= 8 && L > bestL && tpms::decodeAny(bytes, maxbytes, cur, family)) {
            best = cur;
            bestL = L;
        }
    }
    if (bestL) out = best;
    return bestL;
}

static const char* TAG __attribute__((unused)) = "Tpms";

TpmsModule& TpmsModule::instance() {
    static TpmsModule inst;
    return inst;
}

bool TpmsModule::seenRecently(uint32_t id) {
    for (int i = 0; i < recentCount_; i++) {
        if (recentIds_[i] == id) return true;
    }
    if (recentCount_ < kRecentMax) {
        recentIds_[recentCount_++] = id;
    } else {
        // Session set full — overwrite oldest (simple ring).
        recentIds_[seen_ % kRecentMax] = id;
    }
    return false;
}

// ---------------------------------------------------------------------------
bool TpmsModule::start(int module, float freqMHz) {
    if (active_) return true;
    module_ = module;
    freqMHz_ = freqMHz;
    numRadios_ = 1;
    modules_[0] = module;
    freqs_[0] = freqMHz;
    packets_ = 0;
    seen_ = 0;
    edges_ = 0;
    recentCount_ = 0;
    memset(pendingIds_, 0, sizeof(pendingIds_));
    pendingNext_ = 0;
    memset(heldIds_, 0, sizeof(heldIds_));
    lastValidatedAt_ = 0;

    if (!CC1101Worker::startTpmsRX(module_, freqMHz_)) {
        ESP_LOGE(TAG, "startTpmsRX failed on module %d", module_);
        return false;
    }

    active_ = true;
    BaseType_t r = xTaskCreatePinnedToCore(
        decodeTask, "TpmsDecode", 4096, this, 2, &taskHandle_, 1 /* core 1 */);
    if (r != pdPASS) {
        active_ = false;
        CC1101Worker::stopTpmsRX(module_);
        ESP_LOGE(TAG, "decode task create failed");
        return false;
    }
    ESP_LOGI(TAG, "TPMS sniffer started (module %d, %.2f MHz)", module_, freqMHz_);
    sendStatus();
    return true;
}

bool TpmsModule::startBoth(float freqA, float freqB) {
    if (active_) return true;
    numRadios_ = 2;
    modules_[0] = 0; freqs_[0] = freqA;
    modules_[1] = 1; freqs_[1] = freqB;
    module_ = 0; freqMHz_ = freqA;    // primary, for legacy single-field status
    packets_ = 0;
    seen_ = 0;
    edges_ = 0;
    recentCount_ = 0;
    memset(pendingIds_, 0, sizeof(pendingIds_));
    pendingNext_ = 0;
    memset(heldIds_, 0, sizeof(heldIds_));
    lastValidatedAt_ = 0;

    if (!CC1101Worker::startTpmsRX(modules_[0], freqs_[0])) {
        ESP_LOGE(TAG, "startTpmsRX failed on module %d (%.2f MHz)", modules_[0], freqs_[0]);
        numRadios_ = 1;
        return false;
    }
    if (!CC1101Worker::startTpmsRX(modules_[1], freqs_[1])) {
        ESP_LOGE(TAG, "startTpmsRX failed on module %d (%.2f MHz)", modules_[1], freqs_[1]);
        CC1101Worker::stopTpmsRX(modules_[0]);
        numRadios_ = 1;
        return false;
    }

    active_ = true;
    BaseType_t r = xTaskCreatePinnedToCore(
        decodeTask, "TpmsDecode", 4096, this, 2, &taskHandle_, 1 /* core 1 */);
    if (r != pdPASS) {
        active_ = false;
        CC1101Worker::stopTpmsRX(modules_[0]);
        CC1101Worker::stopTpmsRX(modules_[1]);
        numRadios_ = 1;
        ESP_LOGE(TAG, "decode task create failed");
        return false;
    }
    ESP_LOGI(TAG, "TPMS both-band started (mod0 %.2f + mod1 %.2f MHz)", freqs_[0], freqs_[1]);
    sendStatus();
    return true;
}

void TpmsModule::stop() {
    if (!active_) {
        sendStatus();
        return;
    }
    active_ = false;
    for (int i = 0; i < 30 && taskHandle_ != nullptr; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    for (int i = 0; i < numRadios_; i++) CC1101Worker::stopTpmsRX(modules_[i]);
    numRadios_ = 1;
    ESP_LOGI(TAG, "TPMS sniffer stopped (%u packets, %u sensors)", packets_, seen_);
    sendStatus();
}

// ---------------------------------------------------------------------------
void TpmsModule::decodeTask(void* param) {
    TpmsModule* self = static_cast<TpmsModule*>(param);
    const int nr = self->numRadios_;
    // Per-radio decode state; each radio watches its own band and alternates
    // FSK/OOK independently. processPacket/releaseHeld/recentIds_ etc. are SHARED
    // but touched only by THIS single task, so no locking is needed.
    struct RadioState {
        int module; float freq; bool ookMode;
        uint32_t modTimer; uint32_t lastData;
        std::vector<unsigned long> seg;
    } rs[2];
    for (int i = 0; i < nr; i++) {
        rs[i].module   = self->modules_[i];
        rs[i].freq     = self->freqs_[i];
        rs[i].ookMode  = false;           // start FSK, alternate to OOK
        rs[i].modTimer = millis();
        rs[i].lastData = millis();
        rs[i].seg.reserve(kMaxSegment);
    }
    std::vector<unsigned long> chunk;
    chunk.reserve(MAX_SAMPLES_BUFFER);
    uint32_t statusTimer = millis();
#ifdef TPMS_PULSE_DEBUG
    uint32_t dbgHist[24] = {0};
    int dbgMaxRun = 0;
#endif

    // WHY STREAMING: in CC1101 async RX with no carrier-sense squelch, GDO0
    // toggles on noise continuously. The old code waited for 100 ms of line
    // silence to call a burst "complete" -- which never happens in FSK noise --
    // and the ISR buffer filled with noise so a real packet was never stored.
    // Result on the road: packets=0. Now we drain the ISR buffer every 10 ms and
    // cut the pulse stream into runs whose widths are whole multiples of the
    // TPMS half-bit period (1..8 T, so the preamble->sync pulse that carries the
    // first ID half-bit stays in the run); noise can't sustain such a run, a
    // TPMS frame does. Simulated: 200/200 jittered frames found in noise, 0
    // false runs in 1M noise pulses.
    auto flush = [&](RadioState& r) {
        float T = r.ookMode ? kHalfBitOokUs : kHalfBitFskUs;
        if ((int)r.seg.size() >= kMinSegment) self->processPacket(r.seg, T, -50);
        r.seg.clear();
    };

    while (self->active_) {
        for (int i = 0; i < nr; i++) {
            RadioState& r = rs[i];
            float T = r.ookMode ? kHalfBitOokUs : kHalfBitFskUs;
            // Alternate modulation every ~4 s so each band hears both TPMS
            // families: FSK (Toyota/Ford/Hyundai ~52us) and OOK (Schrader ~120us).
            if (millis() - r.modTimer >= 4000) {
                r.modTimer = millis();
                flush(r);
                r.ookMode = !r.ookMode;
                CC1101Worker::retuneTpms(r.module, r.freq, r.ookMode);
                continue;
            }

            portMUX_TYPE& mux = CC1101Worker::getSamplesMux(r.module);
            ReceivedSamples& rxData = CC1101Worker::getSamples(r.module);
            chunk.clear();
            taskENTER_CRITICAL(&mux);
            chunk.assign(rxData.samples.begin(), rxData.samples.end());
            rxData.samples.clear();     // keep lastReceiveTime: next edge stays valid
            taskEXIT_CRITICAL(&mux);

            if (!chunk.empty()) {
                r.lastData = millis();
                self->edges_ += chunk.size();
                for (unsigned long d : chunk) {
#ifdef TPMS_PULSE_DEBUG
                    { int bk = d / 13; if (bk > 23) bk = 23; dbgHist[bk]++; }
                    if ((int)r.seg.size() + 1 > dbgMaxRun) dbgMaxRun = r.seg.size() + 1;
#endif
                    float ratio = (float)d / T;
                    int n = (int)lroundf(ratio);
                    bool fits = n >= 1 && n <= 8 && fabsf(ratio - (float)n) <= 0.40f;
                    if (fits) {
                        if ((int)r.seg.size() < kMaxSegment) r.seg.push_back(d);
                        else flush(r);
                    } else {
                        flush(r);
                    }
                }
            } else if (!r.seg.empty() && millis() - r.lastData > 20) {
                flush(r);            // line went quiet (OOK squelch) after a run
            }
        }

        self->releaseHeld();

        if (millis() - statusTimer >= 2000) {
            statusTimer = millis();
#ifdef TPMS_PULSE_DEBUG
            Serial.printf("TPMSDBG maxrun=%d hist13us:", dbgMaxRun);
            for (int k = 0; k < 24; k++) { Serial.printf(" %lu", (unsigned long)dbgHist[k]); dbgHist[k] = 0; }
            Serial.println();
            dbgMaxRun = 0;
#endif
            self->sendStatus();
            self->edges_ = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Heap-leak fix: vTaskDelete(nullptr) terminates this task WITHOUT unwinding
    // the C++ stack, so the destructors of the local heap-backed vectors below
    // would never run — leaking ~10 KB (chunk reserves 2048*4B, seg too) on every
    // TPMS start/stop. Free their storage explicitly first.
    { std::vector<unsigned long>().swap(chunk); }
    for (int i = 0; i < nr; i++) { std::vector<unsigned long>().swap(rs[i].seg); }

    self->taskHandle_ = nullptr;
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// Generic TPMS fingerprinter. Input: alternating HIGH/LOW pulse durations (us).
// Output: a stable 32-bit ID fingerprint (the start of the Manchester-decoded
// payload, which in virtually all TPMS protocols is the fixed sensor ID).
// Designed for CONSISTENCY per sensor, not semantic decode of pressure/temp.
// ---------------------------------------------------------------------------
// Demodulate a captured pulse burst and decode it. Fills `out` and returns true
// if any ID was recovered (protocol>0 = validated decode, 0 = generic
// fingerprint). Pure w.r.t. BLE/state so it can be unit-tested on-device.
bool TpmsModule::demodPulses(const std::vector<unsigned long>& pulses, TpmsDecoded& out,
                             float tHint) {
    // 1. Base half-bit period T. With a hint (live capture), refine it as the
    //    mean of the single-period pulses (absorbs sensor crystal error);
    //    without one (self-test), use the minimum pulse width.
    float T;
    if (tHint > 0.0f) {
        float sum = 0.0f; int cnt = 0;
        for (unsigned long d : pulses) {
            if (lroundf((float)d / tHint) == 1) { sum += (float)d; cnt++; }
        }
        T = cnt >= 8 ? sum / cnt : tHint;
    } else {
        unsigned long minDur = 1000000;
        for (unsigned long d : pulses) {
            if (d >= 20 && d < minDur) minDur = d;
        }
        if (minDur < 20 || minDur > 1000) return false;
        T = (float)minDur;
    }

    // 2. Expand pulses into a half-bit LEVEL stream (levels alternate, start HIGH).
    static uint8_t half[2048];
    int nh = 0;
    bool level = true;
    for (unsigned long d : pulses) {
        int n = (int)lroundf((float)d / T);
        if (n < 1) n = 1;
        if (n > 8) n = 8;
        for (int k = 0; k < n && nh < (int)sizeof(half); k++) half[nh++] = level ? 1 : 0;
        level = !level;
    }
    // The pulse after the last one in the segment is the idle line, which is
    // never a whole multiple of T, so the segmenter cut it off. If the frame's
    // final bit ended on a transition, its second half-bit is lost with it.
    // Restore one half-bit of the next level (found OTA: Ford checksum byte
    // was always missing its last bit).
    if (nh < (int)sizeof(half)) half[nh++] = level ? 1 : 0;
    if (nh < 128) return false;

    // Decoder family from the half-bit hint: ~52us = FSK, ~120us = OOK,
    // no hint (self-test) = try all.
    const int family = tHint <= 0.0f ? 0 : (tHint > 90.0f ? 2 : 1);

    // 2b. Honda (proto 6): rtl_433 locates Honda by a 23-bit raw desync MARKER
    //     (0xDA 0xE3 0x54) a Manchester decoder can't produce, then decodes 8
    //     Manchester bytes after it. Honda's CRC-8/SMBUS init 0x00 is too weak
    //     for the generic anchor path (it cross-matches other protocols), so it
    //     is decoded ONLY when its marker is found — making it self-anchored and
    //     ghost-free. Searched in both polarities (RX inversion varies).
    if (family != 2) {   // Honda is FSK
        static const uint8_t mk[23] = {1,1,0,1,1,0,1,0, 1,1,1,0,0,0,1,1, 0,1,0,1,0,1,0};
        for (int inv = 0; inv < 2 && !out.ok; inv++) {
            for (int s = 0; s + 23 + 128 <= nh; s++) {
                bool m = true;
                for (int k = 0; k < 23; k++)
                    if ((uint8_t)(half[s + k] ^ inv) != mk[k]) { m = false; break; }
                if (!m) continue;
                uint8_t hb[64]; int nbit = 0; int i = s + 23;
                while (i + 1 < nh && nbit < 64) {
                    uint8_t a = (uint8_t)(half[i] ^ inv), b = (uint8_t)(half[i + 1] ^ inv);
                    if (a == 1 && b == 0) { hb[nbit++] = 1; i += 2; }
                    else if (a == 0 && b == 1) { hb[nbit++] = 0; i += 2; }
                    else break;   // violation -> not a clean payload here
                }
                if (nbit < 64) continue;
                uint8_t by[8];
                for (int B = 0; B < 8; B++) {
                    uint8_t v = 0;
                    for (int k = 0; k < 8; k++) v = (uint8_t)((v << 1) | hb[B * 8 + k]);
                    by[B] = v;
                }
                if (tpms::decodeHonda(by, 8, out) &&
                    tpms::plausibleId(out.id, 32) &&
                    tpms::plausibleReading(out.pressureKpa, out.tempC)) {
                    return true;
                }
                out = TpmsDecoded();   // reset on a failed candidate
            }
        }
    }

    // 2c. Nissan (proto 9): dedicated 36-bit preamble search (0xF5 55 55 55 E0),
    //     then 5 Manchester bytes. Its checksum is only 2 bits, so it is decoded
    //     ONLY when its specific preamble is found (self-anchored, ghost-free).
    if (family != 2) {
        static const uint8_t np[36] = {1,1,1,1,0,1,0,1, 0,1,0,1,0,1,0,1,
                                       0,1,0,1,0,1,0,1, 0,1,0,1,0,1,0,1, 1,1,1,0};
        for (int inv = 0; inv < 2 && !out.ok; inv++) {
            for (int s = 0; s + 36 + 80 <= nh; s++) {
                bool m = true;
                for (int k = 0; k < 36; k++)
                    if ((uint8_t)(half[s + k] ^ inv) != np[k]) { m = false; break; }
                if (!m) continue;
                uint8_t hb[40]; int nbit = 0; int i = s + 36;
                while (i + 1 < nh && nbit < 40) {
                    uint8_t a = (uint8_t)(half[i] ^ inv), b = (uint8_t)(half[i + 1] ^ inv);
                    if (a == 1 && b == 0) { hb[nbit++] = 1; i += 2; }
                    else if (a == 0 && b == 1) { hb[nbit++] = 0; i += 2; }
                    else break;
                }
                if (nbit < 40) continue;
                uint8_t by[5];
                for (int B = 0; B < 5; B++) {
                    uint8_t v = 0;
                    for (int k = 0; k < 8; k++) v = (uint8_t)((v << 1) | hb[B * 8 + k]);
                    by[B] = v;
                }
                if (tpms::decodeNissan(by, 5, out) &&
                    tpms::plausibleId(out.id, 24) &&
                    tpms::plausibleReading(out.pressureKpa, out.tempC)) {
                    return true;
                }
                out = TpmsDecoded();
            }
        }
    }

    // 2d. Toyota (proto 2): rtl_433 tpms_toyota uses DIFFERENTIAL Manchester, not
    //     the standard Manchester the generic path below does — so real Toyota
    //     frames are heard but never decoded without this. Anchored on the 14
    //     half-bit preamble (0101 0101 0011 11), then the exact rtl_433
    //     differential-Manchester bit recovery, then the Toyota byte decoder.
    //     The 1-byte CRC + pressure1==pressure2 constraint make a false match
    //     near-impossible, so a tolerant preamble search (<=2 half-bit slips) is
    //     safe and robust to real-world jitter. (FSK; verified against a real
    //     rtl_433 Toyota capture over the air.)
    if (family != 2) {
        static const uint8_t tpre[14] = {0,1,0,1,0,1,0,1, 0,0,1,1,1,1};
        for (int inv = 0; inv < 2 && !out.ok; inv++) {
            for (int s = 0; s + 14 + 144 <= nh; s++) {
                int miss = 0;
                for (int k = 0; k < 14; k++)
                    if ((uint8_t)(half[s + k] ^ inv) != tpre[k]) { if (++miss > 2) break; }
                if (miss > 2) continue;
                // rtl_433 bitbuffer_differential_manchester_decode, on half-bits.
                uint8_t db[80]; int nb = 0;
                int ipos = s + 14, bit2 = 0;
                auto G = [&](int i) { return (int)(half[i] ^ inv); };
                while (ipos + 2 < nh && nb < 80) {           // sync phase
                    int b1 = G(ipos), b2 = G(ipos + 1), b3 = G(ipos + 2); ipos += 2;
                    if (b1 != b2) {
                        if (b2 != b3) { db[nb++] = 0; }
                        else { bit2 = b1; ipos -= 1; break; }
                    } else { bit2 = 1 - b1; ipos -= 2; break; }
                }
                while (ipos + 1 < nh && nb < 80) {            // data phase
                    int b1 = G(ipos); ipos += 1;
                    if (b1 == bit2) break;                    // clock lost
                    bit2 = G(ipos); ipos += 1;
                    db[nb++] = (b1 == bit2) ? 1 : 0;
                }
                if (nb < 72) continue;
                uint8_t by[9];
                for (int B = 0; B < 9; B++) {
                    uint8_t v = 0;
                    for (int k = 0; k < 8; k++) v = (uint8_t)((v << 1) | (db[B * 8 + k] & 1));
                    by[B] = v;
                }
                if (tpms::decodeToyota(by, 9, out) &&
                    tpms::plausibleId(out.id, 32) &&
                    tpms::plausibleReading(out.pressureKpa, out.tempC)) {
                    return true;
                }
                out = TpmsDecoded();
            }
        }
    }

    // 3. Manchester-decode half-bit pairs into a bit array (realign on
    //    violation), then 4. try the real protocol decoders at every bit
    //    alignment (normal + inverted). Both half-bit phases are tried: on air
    //    the first frame of a burst sometimes locks to the wrong phase and a
    //    preamble of equal-width pulses never forces a violation to resync.
    static uint8_t bitbuf[256];
    static uint8_t firstBits[256];
    int firstNb = -1;
    int firstClean = 0;   // cleanLen of the first phase, for the generic gate
    int i0 = 0, run = 1;
    while (i0 + 1 < nh && half[i0] == half[i0 + 1] && run < 64) { i0++; run++; }
    if (run > 8) i0++;                             // skip preamble, land on an edge
    int bestL = 0;
    for (int phase = 0; phase < 2; phase++) {
        int nb = 0;
        int i = i0 + phase;
        int guard = 0;
        int cleanLen = 0; bool broke = false;   // leading bits from unbroken pairs
        while (i + 1 < nh && nb < (int)sizeof(bitbuf) && guard < nh * 2) {
            guard++;
            uint8_t a = half[i], b = half[i + 1];
            if (a == 1 && b == 0) { bitbuf[nb++] = 1; i += 2; if (!broke) cleanLen = nb; }
            else if (a == 0 && b == 1) { bitbuf[nb++] = 0; i += 2; if (!broke) cleanLen = nb; }
            else { broke = true; i += 1; }
        }
        if (firstNb < 0) { firstNb = nb; memcpy(firstBits, bitbuf, nb); firstClean = cleanLen; }
        if (nb < 64) continue;
        // Anchor to the preamble; keep the decode with the longest preamble lock
        // across both Manchester phases. A passing checksum + plausible reading at
        // the anchored offset is a strong positive -> pressure/temp valid.
        TpmsDecoded cur;
        int L = anchorDecodeBits(bitbuf, nb, cleanLen, cur, family);
        if (L > bestL) { bestL = L; out = cur; }
    }
    if (bestL) return true;
    if (firstNb < 64) return false;

    // 5. No protocol validated -> generic fingerprint for a trackable ID even for
    //    protocols we don't yet decode. Anchor it past the preamble (the leading
    //    constant run) so the fingerprint is the payload start, not the preamble.
    memcpy(bitbuf, firstBits, firstNb);
    int lead = 1;
    while (lead < firstNb && bitbuf[lead] == bitbuf[0]) lead++;
    int goff = (lead >= 12 && lead + 1 + 32 <= firstNb) ? lead + 1 : 0;
    if (goff + 32 > firstNb) return false;
    // Jitter gate: only fingerprint when the whole 32-bit ID region came from an
    // UNBROKEN Manchester run. RX timing jitter mis-sizes a pulse, which breaks a
    // pair and shifts every bit after it; such a frame yields a garbage ID (a
    // phantom "follower"). Requiring the ID to sit inside the clean leading run
    // drops those corrupted frames instead of surfacing a ghost. (Validated
    // protocol frames are unaffected — they decode above, before this path.)
    if (goff + 32 > firstClean) return false;
    uint32_t id = 0;
    for (int k = 0; k < 32; k++) id = (id << 1) | bitbuf[goff + k];
    if (!tpms::plausibleId(id, 32)) return false;   // preamble / garbage pattern
    out.ok = true;
    out.id = id;
    out.pressureKpa = 0xFFFF;
    out.tempC = -128;
    out.protocol = 0;
    return true;
}

void TpmsModule::processPacket(const std::vector<unsigned long>& pulses, float T, int rssi) {
    if (packets_ < 0xFFFF) packets_++;
#ifdef TPMS_PULSE_DEBUG
    Serial.printf("TPMSSEG T=%.0f n=%u:", T, (unsigned)pulses.size());
    for (unsigned long d : pulses) Serial.printf(" %lu", d);
    Serial.println();
#endif
    TpmsDecoded d;
    if (!demodPulses(pulses, d, T)) return;
    uint32_t now = millis();

    if (d.protocol == 0) {
        // Generic (unknown-protocol) fingerprint. Under RX timing jitter a KNOWN
        // sensor's frame mis-aligns and lands here with a garbage ID — a phantom
        // "follower". Two guards:
        //   (a) inside an active validated burst (a protocol decoded in the last
        //       2 s) it is a corrupted repeat of that car — drop it outright;
        //   (b) otherwise HOLD it ~1.2 s instead of showing instantly. If a
        //       validated decode of the same burst arrives in that window it
        //       clears the hold (see below), killing the PRE-validation phantom
        //       that used to leak before the first good frame established the
        //       burst. If nothing validates, releaseHeld() surfaces it as a real
        //       unknown-protocol sensor. Known-make sensors are never delayed —
        //       they decode to a protocol and show instantly below.
        if (lastValidatedAt_ != 0 && (now - lastValidatedAt_) < 2000) return;
        if (!seenRecently(d.id)) holdGeneric(d.id, now);
        return;
    }

    // Validated protocol decode = ground truth for this burst. Drop any generics
    // currently held: they are jitter-corrupted repeats of THIS car, not new
    // vehicles, so one car can't spawn phantom followers.
    lastValidatedAt_ = now;
    memset(heldIds_, 0, sizeof(heldIds_));

    // IMMEDIATE: show the validated sensor the instant it is heard. seenRecently()
    // only stops one ID from spamming the list; a brand-new sensor is always
    // surfaced on its first frame.
    if (seenRecently(d.id)) return;
    if (seen_ < 0xFFFF) seen_++;
    emitHit(d.id, d.pressureKpa, d.tempC, (int8_t)rssi, d.protocol, 1);
}

// Park a generic ID in the hold buffer (dedup; evict the oldest slot if full).
// releaseHeld() emits it after kGenericHoldMs unless a validated decode clears it.
void TpmsModule::holdGeneric(uint32_t id, uint32_t now) {
    for (int k = 0; k < kHeldMax; k++)
        if (heldIds_[k] == id) { heldAt_[k] = now; return; }   // already held, refresh
    int slot = 0; uint32_t oldest = 0xFFFFFFFFu;
    for (int k = 0; k < kHeldMax; k++) {
        if (heldIds_[k] == 0) { slot = k; break; }
        if (heldAt_[k] < oldest) { oldest = heldAt_[k]; slot = k; }
    }
    heldIds_[slot] = id; heldAt_[slot] = now;
}

void TpmsModule::releaseHeld() {
    uint32_t now = millis();
    for (int k = 0; k < kHeldMax; k++) {
        if (heldIds_[k] == 0 || now - heldAt_[k] < kGenericHoldMs) continue;
        // Survived the hold window without a validated decode clearing it ->
        // a genuine unknown-protocol sensor, surface it now.
        uint32_t id = heldIds_[k];
        heldIds_[k] = 0;
        if (seenRecently(id)) continue;
        if (seen_ < 0xFFFF) seen_++;
        emitHit(id, 0xFFFF, -128, -50, 0, 0);
    }
}

bool TpmsModule::demodSelfTest() {
    // Manchester-encode the known Ford vector into a synthetic pulse train and
    // confirm the full demod->decode chain recovers it. Encode convention
    // matches the decoder: bit 1 -> half-bits (1,0), bit 0 -> (0,1).
    const uint8_t v[8] = {0x12, 0x34, 0x56, 0x78, 0x7C, 0x47, 0x04, 0xDB};
    const float T = 52.0f;
    std::vector<uint8_t> H;
    for (int k = 0; k < 12; k++) H.push_back(0);        // preamble run
    for (int by = 0; by < 8; by++) {
        for (int bit = 7; bit >= 0; bit--) {
            uint8_t b = (v[by] >> bit) & 1;
            if (b) { H.push_back(1); H.push_back(0); }
            else   { H.push_back(0); H.push_back(1); }
        }
    }
    // half-bit levels -> pulse widths (merge equal runs)
    std::vector<unsigned long> pulses;
    int j = 0;
    while (j < (int)H.size()) {
        int run = 1;
        while (j + run < (int)H.size() && H[j + run] == H[j]) run++;
        pulses.push_back((unsigned long)lroundf(run * T));
        j += run;
    }
    TpmsDecoded out;
    bool ok = demodPulses(pulses, out);
    return ok && out.id == 0x12345678u && out.protocol == 1;
}

void TpmsModule::emitHit(uint32_t sensorId, uint16_t pressureKpa, int8_t tempC,
                         int8_t rssi, uint8_t protocol, uint8_t flags) {
    BinaryTpmsHit hit;
    hit.sensorId = sensorId;
    hit.pressure_kpa = pressureKpa;
    hit.temperature_c = tempC;
    hit.rssi = rssi;
    hit.protocol = protocol;
    hit.flags = flags;
    ClientsManager::getInstance().notifyAllBinary(
        NotificationType::TpmsEvent, (const uint8_t*)&hit, sizeof(hit));
    ESP_LOGI(TAG, "TPMS sensor 0x%08X (proto=%u)", (unsigned)sensorId, protocol);
}

void TpmsModule::sendStatus() {
    BinaryTpmsStatus st;
    st.active = active_ ? 1 : 0;
    st.module = (numRadios_ == 2) ? 2 : (uint8_t)module_;  // 2 = both-band mode
    st.freqx100 = (uint16_t)lroundf(freqMHz_ * 100.0f);
    st.seen = seen_;
    st.packets = packets_;
    // Radio activity over the last 2 s (edges/256, capped). 0 = the CC1101 is
    // delivering nothing at all (dead radio / wrong GDO), useful in the field.
    uint32_t e = edges_ / 256;
    st.reserved = (uint8_t)(e > 255 ? 255 : e);
    ClientsManager::getInstance().notifyAllBinary(
        NotificationType::TpmsEvent, (const uint8_t*)&st, sizeof(st));
}
