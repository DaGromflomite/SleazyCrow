#include "modules/tpms/TpmsModule.h"

#include <math.h>
#include "esp_log.h"
#include "modules/CC1101_driver/CC1101_Worker.h"
#include "core/ble/ClientsManager.h"
#include "BinaryMessages.h"

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
    packets_ = 0;
    seen_ = 0;
    recentCount_ = 0;

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

void TpmsModule::stop() {
    if (!active_) {
        sendStatus();
        return;
    }
    active_ = false;
    for (int i = 0; i < 30 && taskHandle_ != nullptr; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    CC1101Worker::stopTpmsRX(module_);
    ESP_LOGI(TAG, "TPMS sniffer stopped (%u packets, %u sensors)", packets_, seen_);
    sendStatus();
}

// ---------------------------------------------------------------------------
void TpmsModule::decodeTask(void* param) {
    TpmsModule* self = static_cast<TpmsModule*>(param);
    const int module = self->module_;
    portMUX_TYPE& mux = CC1101Worker::getSamplesMux(module);
    ReceivedSamples& rxData = CC1101Worker::getSamples(module);
    std::vector<unsigned long> local;
    uint32_t statusTimer = millis();

    while (self->active_) {
        size_t count = 0;
        unsigned long lastRx = 0;
        taskENTER_CRITICAL(&mux);
        count = rxData.samples.size();
        lastRx = rxData.lastReceiveTime;
        taskEXIT_CRITICAL(&mux);

        // Burst complete when the line has been quiet > MAX_SIGNAL_DURATION.
        bool complete = false;
        if (count >= 2 && lastRx > 0) {
            if ((micros() - lastRx) > MAX_SIGNAL_DURATION) complete = true;
        }

        if (complete) {
            local.clear();
            taskENTER_CRITICAL(&mux);
            local.assign(rxData.samples.begin(), rxData.samples.end());
            rxData.samples.clear();
            rxData.lastReceiveTime = 0;
            taskEXIT_CRITICAL(&mux);
            if (local.size() >= 40) {            // ignore noise bursts
                self->processPacket(local, -50);
            }
        }

        if (millis() - statusTimer >= 2000) {
            statusTimer = millis();
            self->sendStatus();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    self->taskHandle_ = nullptr;
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// Generic TPMS fingerprinter. Input: alternating HIGH/LOW pulse durations (us).
// Output: a stable 32-bit ID fingerprint (the start of the Manchester-decoded
// payload, which in virtually all TPMS protocols is the fixed sensor ID).
// Designed for CONSISTENCY per sensor, not semantic decode of pressure/temp.
// ---------------------------------------------------------------------------
void TpmsModule::processPacket(const std::vector<unsigned long>& pulses, int rssi) {
    packets_++;

    // 1. Estimate the base half-bit period T = robust minimum pulse width.
    unsigned long minDur = 1000000;
    for (unsigned long d : pulses) {
        if (d >= (unsigned long)MIN_PULSE_DURATION && d < minDur) minDur = d;
    }
    if (minDur < 50 || minDur > 1000) return;    // out of plausible TPMS range
    const float T = (float)minDur;

    // 2. Expand pulses into a half-bit LEVEL stream (levels alternate, start HIGH).
    static uint8_t bits[1024];
    int nb = 0;
    bool level = true;
    for (unsigned long d : pulses) {
        int n = (int)lroundf((float)d / T);
        if (n < 1) n = 1;
        if (n > 6) n = 6;                         // clamp runaway gaps
        for (int k = 0; k < n && nb < (int)sizeof(bits); k++) bits[nb++] = level ? 1 : 0;
        level = !level;
    }
    if (nb < 80) return;                          // too short to hold an ID

    // 3. Skip a leading preamble (long run of identical half-bits), then
    //    Manchester-decode pairs: 1,0 -> 1 ; 0,1 -> 0. Realign on a violation
    //    (0,0 or 1,1) by stepping one half-bit. Deterministic => consistent ID.
    int i = 0;
    // advance past an initial same-level run of > 8 half-bits (preamble/dc)
    int run = 1;
    while (i + 1 < nb && bits[i] == bits[i + 1] && run < 64) { i++; run++; }
    if (run > 8) i++;                             // land on the first edge

    uint32_t id = 0;
    int decoded = 0;
    int guard = 0;
    while (i + 1 < nb && decoded < 32 && guard < nb * 2) {
        guard++;
        uint8_t a = bits[i], b = bits[i + 1];
        if (a == 1 && b == 0) { id = (id << 1) | 1u; decoded++; i += 2; }
        else if (a == 0 && b == 1) { id = (id << 1) | 0u; decoded++; i += 2; }
        else { i += 1; }                          // violation -> realign
    }

    if (decoded < 32) return;                      // couldn't recover a full ID
    if (id == 0 || id == 0xFFFFFFFFu) return;      // reject degenerate

    if (!seenRecently(id)) {
        if (seen_ < 0xFFFF) seen_++;
        // protocol 0 = generic fingerprint; pressure/temp unknown until a
        // per-manufacturer decoder is validated against real captures.
        emitHit(id, 0xFFFF, -128, (int8_t)rssi, 0 /*generic*/, 0 /*no CRC yet*/);
    }
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
    st.module = (uint8_t)module_;
    st.freqx100 = (uint16_t)lroundf(freqMHz_ * 100.0f);
    st.seen = seen_;
    st.packets = packets_;
    st.reserved = 0;
    ClientsManager::getInstance().notifyAllBinary(
        NotificationType::TpmsEvent, (const uint8_t*)&st, sizeof(st));
}
