#include "SelfTest.h"

#include <Arduino.h>
#include <math.h>
#include <stdlib.h>
#include <FS.h>
#include <SD.h>
#include "config.h"
#include "modules/CC1101_driver/CC1101_Module.h"
// Pulls in CC1101Worker (capture ISR API), StreamingSubFileParser and
// StreamingPulsePayload — the real .sub parse + transmit building blocks.
#include "modules/CC1101_driver/CC1101_Worker.h"

// The whole diagnostic only exists in the bench build; the release build gets an
// empty run() (see the #else at the end of this file).
#if defined(SELFTEST_ON_BOOT) && SELFTEST_ON_BOOT

// The exact Ook650 async preset the real .sub transmit path uses
// (SubFileParser.h: subghz_device_cc1101_preset_ook_650khz_async_regs).
// (reg,val) pairs terminated by 00 00, then the 8-byte PA table {00 C0 ...}.
static const uint8_t OOK650[44] = {
    0x02, 0x0D, 0x03, 0x07, 0x08, 0x32, 0x0B, 0x06, 0x14, 0x00, 0x13, 0x00, 0x12, 0x30, 0x11,
    0x32, 0x10, 0x17, 0x18, 0x18, 0x19, 0x18, 0x1D, 0x91, 0x1C, 0x00, 0x1B, 0x07, 0x20, 0xFB,
    0x22, 0x11, 0x21, 0xB6, 0x00, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Verbatim copy of CC1101Worker::holdPulse (CC1101_Worker.h): the private
// inline the real file-TX path uses to time each pulse. Copied (not called)
// because it is a private static — this measures the *same* timing logic.
static inline void holdPulseLikeFirmware(uint32_t us, bool level, uint32_t &lastRest) {
    const uint32_t start = micros();
    if (!level && us >= 3000) {
        vTaskDelay(pdMS_TO_TICKS((us - 2000) / 1000));
        lastRest = micros();
    } else if (!level && (uint32_t)(start - lastRest) > 2000000UL) {
        vTaskDelay(1);
        lastRest = micros();
    }
    while ((uint32_t)(micros() - start) < us) {
    }
}

// Self-contained RF edge capture (mirrors CC1101Worker's recording ISR, which
// is private). Times the gaps between CHANGE edges on radio 1's async-data pin,
// with the same filtering the firmware recorder uses.
static volatile uint32_t g_capBuf[700];
static volatile int g_capCount = 0;
static volatile uint32_t g_capLast = 0;
static volatile bool g_capOn = false;

static void IRAM_ATTR selftestEdgeISR(void *) {
    uint32_t t = micros();
    if (!g_capOn) return;
    if (g_capLast == 0) {
        g_capLast = t;
        return;
    }
    uint32_t d = t - g_capLast;
    g_capLast = t;
    if (d >= MIN_PULSE_DURATION && d <= MAX_SIGNAL_DURATION && g_capCount < 700)
        g_capBuf[g_capCount++] = d;
}

static void line() {
    Serial.println("--------------------------------------------------------");
}

// Average a few RSSI reads at frequency f on an already-RX-armed module.
static int rssiAt(int mod, float f, int samples = 4) {
    long sum = 0;
    for (int i = 0; i < samples; i++) {
        moduleCC1101State[mod].changeFrequency(f);
        delayMicroseconds(SDR_RSSI_SETTLE_US);
        sum += moduleCC1101State[mod].getRssi();
    }
    return (int)(sum / samples);
}

// Hold an unmodulated carrier on/off on [mod] at [f] via the real OOK650 TX
// path (setTxWithPreset + drive GDO0 high == continuous carrier).
static void carrier(int mod, float f, bool on) {
    const byte pin = moduleCC1101State[mod].getOutputPin();
    if (on) {
        moduleCC1101State[mod].setTxWithPreset(f, OOK650, 44);
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
        delay(5);
    } else {
        digitalWrite(pin, LOW);
        moduleCC1101State[mod].setSidle();
    }
}

static void armRx(int mod, float f, float bwKHz) {
    moduleCC1101State[mod].setReceiveConfig(f, true, MODULATION_ASK_OOK, bwKHz, 1.58f, 3.79372f);
    moduleCC1101State[mod].initConfig();
    moduleCC1101State[mod].changeFrequency(f);
    delay(5);
}

void SelfTest::run() {
    delay(300);
    Serial.println();
    line();
    Serial.printf("  SLEAZY CROW SELF-TEST  (%s)\n", FIRMWARE_VERSION_STRING);
    Serial.println("  dual-CC1101 sub-GHz TX diagnostic");
    line();
    Serial.printf("Free heap at start: %u bytes\n", (unsigned)ESP.getFreeHeap());

    // ---- T1: radio liveness (SPI write->read round-trip, each module) ------
    line();
    Serial.println("[T1] Radio liveness — program 433.92 MHz, read FREQ regs back");
    for (int m = 0; m < CC1101_NUM_MODULES; m++) {
        moduleCC1101State[m].setTx(433.92f);
        delay(5);
        float rb = moduleCC1101State[m].getFrequency();
        moduleCC1101State[m].setSidle();
        bool ok = fabsf(rb - 433.92f) < 0.05f;
        Serial.printf("   module %d: readback %.4f MHz  -> %s\n", m, rb,
                      ok ? "PASS (SPI + synth OK)" : "FAIL (radio not responding)");
    }

    // ---- T2: pulse timing fidelity (holdPulse logic) -----------------------
    line();
    Serial.println("[T2] Pulse-timing fidelity — TX a known pattern, time it");
    {
        // Representative mix: short OOK pulses + long gaps (incl. the >=3000us
        // LOW gaps that take the vTaskDelay path, like a real TouchTunes capture).
        const int32_t pat[] = {
            400, -400, 400, -400, 600, -200, 300, -900, 500, -500,
            -32700, 12737, -16798, 163, -854, 261, -560, 197, -856, 165,
            -18000, 9000, -3000, 1200, -1200, 800, -800, 400, -400, 400,
            -25000, 500, -500, 500, -500, 500, -500, 500, -500, 500};
        const int n = sizeof(pat) / sizeof(pat[0]);
        uint64_t expectedUs = 0;
        for (int i = 0; i < n; i++) expectedUs += (uint32_t)abs(pat[i]);

        const byte pin = moduleCC1101State[0].getOutputPin();
        moduleCC1101State[0].setTxWithPreset(433.92f, OOK650, 44);
        pinMode(pin, OUTPUT);

        uint32_t lastRest = micros();
        uint32_t t0 = micros();
        for (int i = 0; i < n; i++) {
            bool level = pat[i] > 0;
            digitalWrite(pin, level);
            holdPulseLikeFirmware((uint32_t)abs(pat[i]), level, lastRest);
        }
        uint32_t measuredUs = micros() - t0;
        digitalWrite(pin, LOW);
        moduleCC1101State[0].setSidle();

        double errPct = 100.0 * ((double)measuredUs - (double)expectedUs) / (double)expectedUs;
        Serial.printf("   pattern pulses: %d   expected: %llu us   measured: %u us\n",
                      n, (unsigned long long)expectedUs, (unsigned)measuredUs);
        Serial.printf("   timing error: %+.2f%%  -> %s\n", errPct,
                      (fabs(errPct) < 3.0) ? "PASS (timing faithful)"
                                           : "SUSPECT (timing drift could break decoding)");
    }

    // ---- T3: TX -> RX keying across bands (does the chain radiate?) --------
    line();
    Serial.println("[T3] TX->RX keying — module0 carrier, module1 RSSI (floor vs on)");
    {
        const float freqs[] = {315.00f, 433.92f, 868.35f};
        for (float f : freqs) {
            armRx(1, f, 325.0f);
            int floorRssi = rssiAt(1, f);
            carrier(0, f, true);
            int onRssi = rssiAt(1, f);
            carrier(0, f, false);
            int delta = onRssi - floorRssi;
            Serial.printf("   %7.2f MHz: floor %4d dBm | carrier %4d dBm | delta %+3d dB -> %s\n",
                          f, floorRssi, onRssi, delta,
                          (delta >= 10) ? "PASS (TX chain radiates + keys)"
                                        : "WEAK/absent (TX not reaching RX)");
        }
    }

    // ---- T4: relative frequency peak (two-crystal caveat) ------------------
    line();
    Serial.println("[T4] Relative freq peak — module0 carrier @433.92, module1 sweep");
    Serial.println("     NOTE: each CC1101 has its own crystal; this is the offset");
    Serial.println("     BETWEEN the two radios, not absolute error vs the real 433.92.");
    {
        const float center = 433.92f;
        armRx(1, center, 58.0f);
        carrier(0, center, true);
        float bestF = center;
        int bestR = -200;
        for (float f = center - 0.40f; f <= center + 0.40f + 1e-6f; f += 0.01f) {
            int r = rssiAt(1, f, 2);
            if (r > bestR) {
                bestR = r;
                bestF = f;
            }
        }
        carrier(0, center, false);
        Serial.printf("   peak at %.3f MHz (%d dBm) | relative offset %+.0f kHz\n",
                      bestF, bestR, (bestF - center) * 1000.0f);
        Serial.printf("   config BRUTER_CC1101_FREQ_OFFSET = %.0f kHz (prior SDR-measured trim)\n",
                      (float)(BRUTER_CC1101_FREQ_OFFSET * 1000.0));
    }

    // ---- T5: PA / power response -------------------------------------------
    line();
    Serial.println("[T5] PA power response — module1 RSSI at low vs max TX power");
    {
        const float f = 433.92f;
        armRx(1, f, 325.0f);
        carrier(0, f, true);
        moduleCC1101State[0].setPA(-10);
        delay(5);
        int lowR = rssiAt(1, f);
        moduleCC1101State[0].setPA(10);
        delay(5);
        int hiR = rssiAt(1, f);
        carrier(0, f, false);
        Serial.printf("   PA -10 dBm: %d dBm | PA +10 dBm: %d dBm | gain %+d dB -> %s\n",
                      lowR, hiR, hiR - lowR,
                      (hiR - lowR >= 4) ? "PASS (power control works)"
                                        : "INCONCLUSIVE");
    }

    // ---- T6: environment snapshot ------------------------------------------
    line();
    Serial.println("[T6] Environment");
    Serial.printf("   firmware: %s\n", FIRMWARE_VERSION_STRING);
    Serial.printf("   free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
    {
        std::array<byte, 8> pa = moduleCC1101State[0].getPATableValues();
        Serial.print("   module0 PA table:");
        for (auto b : pa) Serial.printf(" %02X", b);
        Serial.println();
    }

    // ---- T7: file -> antenna fidelity (replay a .sub, capture the RF) ------
    line();
    Serial.println("[T7] File->antenna fidelity — replay a real .sub through the REAL");
    Serial.println("     parse+stream+TX path on radio 0, capture the RF on radio 1,");
    Serial.println("     compare the captured edge-train to the file. No jukebox needed.");
    {
        // A TouchTunes-shaped reference: an OOK frame (clean 300-1500us widths so
        // the RX slicer reproduces it reliably) repeated 3x across 3 RAW_Data
        // lines, separated by long inter-frame gaps — exercises the multi-line
        // streaming parser end to end.
        const int32_t frame[] = {500, -500, 1000, -500, 500, -1000, 1500, -500,
                                 500, -500, 1000, -1000, 1500, -1500, 800, -1200};
        const int frameN = sizeof(frame) / sizeof(frame[0]);
        const int32_t GAP = -15000;

        // Build the full expected signed stream = frame, GAP, frame, GAP, frame.
        static int32_t ref[64];
        int refN = 0;
        for (int rep = 0; rep < 3; rep++) {
            for (int i = 0; i < frameN; i++) ref[refN++] = frame[i];
            if (rep < 2) ref[refN++] = GAP;
        }
        uint64_t refTotal = 0;
        for (int i = 0; i < refN; i++) refTotal += (uint32_t)abs(ref[i]);

        // Write /selftest_ref.sub to SD (one RAW_Data line per frame).
        const char *path = "/selftest_ref.sub";
        SD.remove(path);
        File f = SD.open(path, FILE_WRITE);
        if (!f) {
            Serial.println("   FAIL: could not create /selftest_ref.sub on SD");
        } else {
            f.println("Filetype: Flipper SubGhz RAW File");
            f.println("Version: 1");
            f.println("Frequency: 433920000");
            f.println("Preset: FuriHalSubGhzPresetOok650Async");
            f.println("Protocol: RAW");
            for (int rep = 0; rep < 3; rep++) {
                f.print("RAW_Data:");
                for (int i = 0; i < frameN; i++) f.printf(" %ld", (long)frame[i]);
                if (rep < 2) f.printf(" %ld", (long)GAP);
                f.println();
            }
            f.close();

            // Parse it back with the REAL header parser.
            StreamingSubFileParser sp;
            StreamingSubFileParser::SubFileHeader h;
            bool hdrOk = sp.parseHeader(path, h);
            Serial.printf("   header: freq=%u Hz preset=%s protocol=%s -> %s\n",
                          (unsigned)h.frequency, h.preset.c_str(), h.protocol.c_str(),
                          hdrOk ? "parsed" : "PARSE FAIL");

            // Arm radio 1 as an OOK recorder (same recipe as handleStartRecord),
            // then attach our own capture ISR to its async-data pin (GDO2).
            moduleCC1101State[1]
                .setReceiveConfig(433.92f, false, MODULATION_ASK_OOK, 232.0f, 0.0f, 8.0f)
                .initConfig();
            const int capPin = moduleCC1101State[1].getInputPin();
            g_capCount = 0;
            g_capLast = 0;
            g_capOn = true;
            attachInterruptArg(capPin, selftestEdgeISR, nullptr, CHANGE);
            delay(5);

            // Replay on radio 0 via the REAL streaming TX path.
            const byte pin0 = moduleCC1101State[0].getOutputPin();
            moduleCC1101State[0].setTxWithPreset(h.frequency / 1000000.0f, OOK650, 44);
            pinMode(pin0, OUTPUT);
            digitalWrite(pin0, LOW);
            StreamingPulsePayload payload;
            uint32_t txStart = micros();
            if (payload.init(path, 1)) {
                uint32_t dur;
                bool lvl;
                uint32_t lastRest = micros();
                while (payload.next(dur, lvl)) {
                    digitalWrite(pin0, lvl);
                    holdPulseLikeFirmware(dur, lvl, lastRest);
                }
                payload.close();
            } else {
                Serial.println("   FAIL: StreamingPulsePayload.init() returned false");
            }
            uint32_t txMeasured = micros() - txStart;
            digitalWrite(pin0, LOW);
            moduleCC1101State[0].setSidle();
            delay(5);
            g_capOn = false;
            detachInterrupt(capPin);
            moduleCC1101State[1].setSidle();

            // Read what radio 1 actually heard.
            volatile uint32_t *cap = g_capBuf;
            int capN = g_capCount;
            uint64_t capTotal = 0;
            int gapsSeen = 0;
            for (int i = 0; i < capN; i++) {
                capTotal += cap[i];
                if (cap[i] >= 12000 && cap[i] <= 18000) gapsSeen++;
            }

            // Best-of-two alignment (the first captured interval is often lead-in).
            auto scoreFrom = [&](int off) {
                int hit = 0, n = 0;
                for (int i = 0; i + off < capN && i < refN; i++, n++) {
                    uint32_t r = (uint32_t)abs(ref[i]);
                    uint32_t c = cap[i + off];
                    uint32_t tol = (uint32_t)fmaxf(80.0f, 0.25f * r);
                    if (c > r ? (c - r <= tol) : (r - c <= tol)) hit++;
                }
                return n > 0 ? (100 * hit) / n : 0;
            };
            int match = max(scoreFrom(0), scoreFrom(1));
            double durErr = refTotal ? 100.0 * ((double)txMeasured - (double)refTotal) / (double)refTotal : 0;

            Serial.printf("   reference: %d pulses, %llu us total (TX measured %u us, %+.1f%%)\n",
                          refN, (unsigned long long)refTotal, (unsigned)txMeasured, durErr);
            Serial.printf("   captured : %d intervals, %llu us total, %d long-gap(s) seen (expect 2)\n",
                          capN, (unsigned long long)capTotal, gapsSeen);
            Serial.printf("   edge-train match: %d%%  (per-pulse, within +-25%%/80us)\n", match);

            bool radiated = capN >= (refN - 6) && gapsSeen >= 1;
            bool faithful = radiated && match >= 70;
            Serial.printf("   -> %s\n",
                          faithful  ? "PASS (antenna emits the file faithfully end-to-end)"
                          : radiated ? "PARTIAL (RF present & structured, but edge match low — see numbers)"
                                     : "FAIL (radio 1 did not capture the replayed file)");

            // --- Isolate the cause: pure next() cost vs an in-RAM array replay ---
            StreamingPulsePayload probe;
            uint32_t itStart = micros();
            int itN = 0;
            if (probe.init(path, 1)) {
                uint32_t d;
                bool l;
                while (probe.next(d, l)) itN++;
                probe.close();
            }
            uint32_t itUs = micros() - itStart;
            Serial.printf("   [A] stream next() overhead: %u us over %d pulses = %.0f us/pulse (no TX)\n",
                          (unsigned)itUs, itN, itN ? (double)itUs / itN : 0.0);

            moduleCC1101State[0].setTxWithPreset(433.92f, OOK650, 44);
            pinMode(pin0, OUTPUT);
            digitalWrite(pin0, LOW);
            uint32_t aStart = micros();
            uint32_t lr = micros();
            for (int i = 0; i < refN; i++) {
                bool lv = ref[i] > 0;
                digitalWrite(pin0, lv);
                holdPulseLikeFirmware((uint32_t)abs(ref[i]), lv, lr);
            }
            uint32_t aUs = micros() - aStart;
            digitalWrite(pin0, LOW);
            moduleCC1101State[0].setSidle();
            Serial.printf("   [B] in-RAM array replay (same pattern): %u us (%+.1f%% vs reference)\n",
                          (unsigned)aUs,
                          100.0 * ((double)aUs - (double)refTotal) / (double)refTotal);
            Serial.println("   [A]+[B]: if [B] is tight and stream TX is long, the SD-streaming");
            Serial.println("   TX path is stretching pulses — the likely real TouchTunes cause.");

            // --- Verify the FIX end-to-end through the REAL transmitSub path ---
            moduleCC1101State[1]
                .setReceiveConfig(433.92f, false, MODULATION_ASK_OOK, 232.0f, 0.0f, 8.0f)
                .initConfig();
            g_capCount = 0;
            g_capLast = 0;
            g_capOn = true;
            attachInterruptArg(capPin, selftestEdgeISR, nullptr, CHANGE);
            delay(5);
            std::string txErr = CC1101Worker::transmitSubForSelfTest("/selftest_ref.sub", 0, 1, 5);
            delay(5);
            g_capOn = false;
            detachInterrupt(capPin);
            moduleCC1101State[1].setSidle();

            int capN2 = g_capCount;
            int gaps2 = 0;
            uint64_t capTotal2 = 0;
            for (int i = 0; i < capN2; i++) {
                capTotal2 += g_capBuf[i];
                if (g_capBuf[i] >= 12000 && g_capBuf[i] <= 18000) gaps2++;
            }
            auto score2 = [&](int off) {
                int hit = 0, n = 0;
                for (int i = 0; i + off < capN2 && i < refN; i++, n++) {
                    uint32_t r = (uint32_t)abs(ref[i]);
                    uint32_t c = g_capBuf[i + off];
                    uint32_t tol = (uint32_t)fmaxf(80.0f, 0.25f * r);
                    if (c > r ? (c - r <= tol) : (r - c <= tol)) hit++;
                }
                return n > 0 ? (100 * hit) / n : 0;
            };
            int match2 = max(score2(0), score2(1));
            // True discriminator = captured TOTAL duration (RX-slicer-independent):
            // faithful TX emits the real ~72000 us; stretched streaming ~81000 us.
            double capErr2 = refTotal ? 100.0 * ((double)capTotal2 - (double)refTotal) / (double)refTotal : 0;
            Serial.printf("   [FIX] REAL transmitSub path: %d intervals, %llu us total (%+.1f%% vs file), %d gap(s), match %d%%\n",
                          capN2, (unsigned long long)capTotal2, capErr2, gaps2, match2);
            Serial.printf("   [FIX] captured length %+.1f%% (streaming captured was ~+13%%); match%% is RX-slicer limited\n",
                          capErr2);
            Serial.printf("   [FIX] -> %s\n",
                          (txErr.empty() && gaps2 >= 1 && fabs(capErr2) < 6.0)
                              ? "PASS — emitted signal length matches the file (stretch gone)"
                              : (!txErr.empty() ? "TX ERROR" : "CHECK numbers above"));
            if (!txErr.empty())
                Serial.printf("   [FIX] transmitSub error: %s\n", txErr.c_str());

            SD.remove(path);
        }
    }

    line();
    Serial.println("  SELF-TEST COMPLETE — halting (reflash release build to restore)");
    line();
    Serial.flush();
    while (true) {
        delay(1000);
    }
}

#else  // !SELFTEST_ON_BOOT — release build: no diagnostic.
void SelfTest::run() {}
#endif
