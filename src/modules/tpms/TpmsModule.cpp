#include "modules/tpms/TpmsModule.h"

#include <math.h>
#include "esp_log.h"
#include "modules/CC1101_driver/CC1101_Worker.h"
#include "modules/tpms/TpmsDecoders.h"
#include "core/ble/ClientsManager.h"
#include "BinaryMessages.h"

// Pack a Manchester-decoded bit array into bytes at every bit-offset (normal and
// inverted) and run the protocol decoders. A wrong alignment can't pass a
// checksum/CRC, so a validated decode is a strong positive.
static bool packAndDecode(const uint8_t* bits, int nb, bool invert, TpmsDecoded& out) {
    uint8_t bytes[12];
    for (int off = 0; off + 64 <= nb; off++) {
        int maxbytes = (nb - off) / 8;
        if (maxbytes > 12) maxbytes = 12;
        for (int B = 0; B < maxbytes; B++) {
            uint8_t v = 0;
            for (int b = 0; b < 8; b++) {
                uint8_t bit = bits[off + B * 8 + b];
                if (invert) bit ^= 1;
                v = (uint8_t)((v << 1) | (bit & 1));
            }
            bytes[B] = v;
        }
        if (maxbytes >= 8 && tpms::decodeAny(bytes, maxbytes, out)) return true;
    }
    return false;
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
    uint32_t modTimer = millis();
    bool ookMode = false;   // start FSK (Toyota/Ford/Hyundai), alternate to OOK

    while (self->active_) {
        // Alternate modulation every ~4 s so we hear both TPMS families:
        // FSK (Toyota/Ford/Hyundai ~52us) and OOK (Schrader/GM/BMW ~120us).
        if (millis() - modTimer >= 4000) {
            modTimer = millis();
            ookMode = !ookMode;
            CC1101Worker::retuneTpms(self->module_, self->freqMHz_, ookMode);
        }
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
// Demodulate a captured pulse burst and decode it. Fills `out` and returns true
// if any ID was recovered (protocol>0 = validated decode, 0 = generic
// fingerprint). Pure w.r.t. BLE/state so it can be unit-tested on-device.
bool TpmsModule::demodPulses(const std::vector<unsigned long>& pulses, TpmsDecoded& out) {
    // 1. Estimate the base half-bit period T = robust minimum pulse width.
    //    (TPMS symbols are ~52us; MIN_PULSE_DURATION filters sub-threshold noise.)
    unsigned long minDur = 1000000;
    for (unsigned long d : pulses) {
        if (d >= (unsigned long)MIN_PULSE_DURATION && d < minDur) minDur = d;
    }
    if (minDur < 40 || minDur > 1000) return false;
    const float T = (float)minDur;

    // 2. Expand pulses into a half-bit LEVEL stream (levels alternate, start HIGH).
    static uint8_t half[2048];
    int nh = 0;
    bool level = true;
    for (unsigned long d : pulses) {
        int n = (int)lroundf((float)d / T);
        if (n < 1) n = 1;
        if (n > 6) n = 6;
        for (int k = 0; k < n && nh < (int)sizeof(half); k++) half[nh++] = level ? 1 : 0;
        level = !level;
    }
    if (nh < 128) return false;

    // 3. Manchester-decode half-bit pairs into a bit array (realign on violation).
    static uint8_t bitbuf[256];
    int nb = 0;
    int i = 0, run = 1;
    while (i + 1 < nh && half[i] == half[i + 1] && run < 64) { i++; run++; }
    if (run > 8) i++;                              // skip preamble, land on an edge
    int guard = 0;
    while (i + 1 < nh && nb < (int)sizeof(bitbuf) && guard < nh * 2) {
        guard++;
        uint8_t a = half[i], b = half[i + 1];
        if (a == 1 && b == 0) { bitbuf[nb++] = 1; i += 2; }
        else if (a == 0 && b == 1) { bitbuf[nb++] = 0; i += 2; }
        else { i += 1; }
    }
    if (nb < 64) return false;

    // 4. Try the real protocol decoders at every alignment (normal + inverted).
    //    A passing checksum/CRC is a strong positive -> pressure/temp valid.
    if (packAndDecode(bitbuf, nb, false, out) || packAndDecode(bitbuf, nb, true, out)) {
        return true;
    }

    // 5. No protocol validated -> generic fingerprint (first 32 bits). Gives a
    //    consistent trackable ID even for protocols we don't yet decode.
    uint32_t id = 0;
    for (int k = 0; k < 32; k++) id = (id << 1) | bitbuf[k];
    if (id == 0 || id == 0xFFFFFFFFu) return false;
    out.ok = true;
    out.id = id;
    out.pressureKpa = 0xFFFF;
    out.tempC = -128;
    out.protocol = 0;
    return true;
}

void TpmsModule::processPacket(const std::vector<unsigned long>& pulses, int rssi) {
    packets_++;
    TpmsDecoded d;
    if (!demodPulses(pulses, d)) return;
    if (seenRecently(d.id)) return;
    if (seen_ < 0xFFFF) seen_++;
    emitHit(d.id, d.pressureKpa, d.tempC, (int8_t)rssi, d.protocol,
            d.protocol ? 1 : 0);
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
    st.module = (uint8_t)module_;
    st.freqx100 = (uint16_t)lroundf(freqMHz_ * 100.0f);
    st.seen = seen_;
    st.packets = packets_;
    st.reserved = 0;
    ClientsManager::getInstance().notifyAllBinary(
        NotificationType::TpmsEvent, (const uint8_t*)&st, sizeof(st));
}
