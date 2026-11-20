#ifndef TPMS_MODULE_H
#define TPMS_MODULE_H

#include <Arduino.h>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "modules/tpms/TpmsDecoders.h"

/**
 * @file TpmsModule.h
 * @brief Tire-Pressure-Monitoring-System (TPMS) sniffer for vehicle stalking detection.
 *
 * Why: every TPMS sensor transmits a FIXED, unencrypted 32-bit ID (Rutgers/USC
 * 2010; IEEE WONS 2026). A vehicle seen repeatedly across trips/locations by the
 * same sensor IDs is a candidate follower. This module receives 2-FSK TPMS
 * packets on 315 MHz (US) / 433.92 MHz (EU) with ONE CC1101, extracts a stable
 * per-sensor ID fingerprint, and streams it to the app over BLE. The app does the
 * "same ID across sessions = following you" tracking + alert.
 *
 * Radio independence: this runs on ONE CC1101 (default module 0) via the proven
 * async-capture path (CC1101Worker::startTpmsRX + the per-module ISR). The OTHER
 * CC1101 stays fully free — separate CS + GDO pins, per-module ISR/buffers; the
 * only shared resource is a microsecond-scale SPI mutex (verified). The decode
 * task is pinned to CORE 1.
 *
 * HONESTY NOTE: the exact CC1101 tuning and the generic Manchester fingerprinter
 * are best-effort and NOT yet validated against a real sensor signal. The module
 * always reports a "packets captured" count so reception can be confirmed against
 * a real vehicle first; per-manufacturer decoders (pressure/temp) come after
 * capture is proven. The ID fingerprint is designed for CONSISTENCY (same sensor
 * -> same ID) rather than semantic correctness, which is what tracking needs.
 */
class TpmsModule {
public:
    static TpmsModule& instance();

    // Start sniffing on the given CC1101 module at freqMHz (315.0 US / 433.92 EU).
    bool start(int module = 0, float freqMHz = 315.0f);
    // Both-antenna mode: one CC1101 per band, BOTH scanning at once — no choice
    // between 315 and 433. Module 0 watches freqA, module 1 watches freqB; each
    // still alternates FSK/OOK on its own band. One decode task drains both.
    bool startBoth(float freqA = 315.0f, float freqB = 433.92f);
    void stop();
    bool isActive() const { return active_; }

    void sendStatus();

    // Demodulate + decode a pulse burst (pure). protocol>0 = validated decode,
    // 0 = generic fingerprint. Exposed for the on-device demod self-test.
    static bool demodPulses(const std::vector<unsigned long>& pulses, TpmsDecoded& out,
                            float tHint = 0.0f);

    // On-device roundtrip test of the FULL demod->decode chain: Manchester-encode
    // the known Ford vector into a synthetic pulse train and confirm it decodes
    // back to ID 0x12345678. Returns true on success.
    static bool demodSelfTest();

private:
    TpmsModule() = default;

    static void decodeTask(void* param);
    void processPacket(const std::vector<unsigned long>& pulses, float T, int rssi);

    // Streaming segmenter tuning (see decodeTask).
    static constexpr float kHalfBitFskUs = 52.0f;   // Ford/Toyota/Hyundai-VDO
    static constexpr float kHalfBitOokUs = 120.0f;  // Schrader family
    static constexpr int   kMinSegment = 48;        // pulses; a TPMS frame is ~90+
    static constexpr int   kMaxSegment = 600;
    void emitHit(uint32_t sensorId, uint16_t pressureKpa, int8_t tempC,
                 int8_t rssi, uint8_t protocol, uint8_t flags);

    volatile bool active_ = false;
    int           module_ = 0;
    float         freqMHz_ = 315.0f;
    int           numRadios_ = 1;             // 1 = one band; 2 = both bands at once
    int           modules_[2] = {0, 1};       // CC1101 module index per radio
    float         freqs_[2]   = {315.0f, 433.92f};
    TaskHandle_t  taskHandle_ = nullptr;

    uint16_t packets_ = 0;   // TPMS-shaped pulse runs heard (reception confidence)
    volatile uint32_t edges_ = 0;  // raw ISR edges in the current 2 s window
    uint16_t seen_ = 0;      // unique sensor IDs this session
    // Small de-dup set of recently reported IDs so one chatty sensor doesn't
    // flood BLE (the app keeps the long-term history).
    static constexpr int kRecentMax = 48;
    uint32_t recentIds_[kRecentMax] = {0};
    // Unconfirmed generic (no-CRC) fingerprints awaiting a repeat.
    static constexpr int kPendingMax = 32;
    uint32_t pendingIds_[kPendingMax] = {0};
    uint32_t pendingAt_[kPendingMax] = {0};
    int pendingNext_ = 0;
    // Generic (unknown-protocol) IDs are held briefly before being shown. If a
    // validated protocol decode arrives in the hold window it clears them (they
    // were jitter-corrupted repeats of that car — pre-burst phantoms); otherwise
    // releaseHeld() surfaces them as a real unknown-protocol sensor.
    static constexpr int kHeldMax = 8;
    static constexpr uint32_t kGenericHoldMs = 1200;
    uint32_t heldIds_[kHeldMax] = {0};
    uint32_t heldAt_[kHeldMax] = {0};
    uint32_t lastValidatedAt_ = 0;
    void releaseHeld();
    void holdGeneric(uint32_t id, uint32_t now);
    int      recentCount_ = 0;

    bool seenRecently(uint32_t id);
};

#endif // TPMS_MODULE_H
