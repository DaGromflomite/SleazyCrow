/**
#include <exception>
 * @file ProtoPirateModule.cpp
 * @brief ProtoPirate module implementation.
 *
 * Manages CC1101 RX, feeds ISR samples to protocol decoders,
 * stores results in history, and notifies BLE clients.
 */

#include "ProtoPirateModule.h"
#include "modules/subghz_function/StreamingSubFileParser.h"
#include "FlipperSubFile.h"
#include "protocols/PPKeystore.h"
#include "protocols/KeeloqCipher.h"
#include <SD.h>
#include <cstring>
#include <cstdlib>
#include <algorithm>

// Static allocation for FreeRTOS task
StackType_t ProtoPirateModule::taskStack_[ProtoPirateModule::TASK_STACK_SIZE];
StaticTask_t ProtoPirateModule::taskTcb_;

// Static allocation for the dedicated file-load task
StackType_t ProtoPirateModule::loadStack_[ProtoPirateModule::LOAD_STACK_SIZE];
StaticTask_t ProtoPirateModule::loadTcb_;
char ProtoPirateModule::loadPath_[128];
volatile bool ProtoPirateModule::loadBusy_ = false;

void ProtoPirateModule::loadFileTask(void* param) {
    (void)param;
    // A malformed or oversized file must never crash the device: swallow any
    // exception (e.g. bad_alloc from a huge RAW file) and fail gracefully.
    try {
        getInstance().loadSubFile(loadPath_);
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "loadFileTask: exception loading %s: %s", loadPath_, e.what());
    } catch (...) {
        ESP_LOGE(TAG, "loadFileTask: unknown exception loading %s", loadPath_);
    }
    loadBusy_ = false;
    vTaskDelete(nullptr);
}

bool ProtoPirateModule::loadSubFileAsync(const char* filePath) {
    if (loadBusy_) {
        ESP_LOGW(TAG, "loadSubFileAsync: a load is already running");
        return false;
    }
    if (!filePath || filePath[0] == '\0') return false;
    loadBusy_ = true;
    strncpy(loadPath_, filePath, sizeof(loadPath_) - 1);
    loadPath_[sizeof(loadPath_) - 1] = '\0';
    // Pinned to Core 1 (APP core), like the other RF tasks.
    TaskHandle_t h = xTaskCreateStaticPinnedToCore(
        loadFileTask, "PPFileLoad", LOAD_STACK_SIZE, nullptr, 2,
        loadStack_, &loadTcb_, 1);
    if (h == nullptr) {
        loadBusy_ = false;
        ESP_LOGE(TAG, "loadSubFileAsync: failed to create task");
        return false;
    }
    return true;
}

bool ProtoPirateModule::init() {
    if (mutex_) return true;  // Already initialized

    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return false;
    }

    // Instantiate all registered protocol decoders
    const auto& factories = ppGetRegisteredProtocols();
    decoders_.reserve(factories.size());
    for (auto& entry : factories) {
        decoders_.push_back(entry.creator());
    }
    ESP_LOGI(TAG, "Initialized with %zu protocol decoders", decoders_.size());

    // Load manufacturer/learning keys from SD (if present) into the shared
    // keystore, so rolling-code decoders can recover counters and the
    // forward-code generator can synthesize next codes. Tester-supplied file.
    loadKeystoreFromSd();

    return true;
}

bool ProtoPirateModule::startDecode(int ccModule, float frequency) {
    if (state_ == PPState::Decoding) {
        ESP_LOGW(TAG, "Already decoding — stop first");
        return false;
    }

    if (ccModule < 0 || ccModule >= CC1101_NUM_MODULES) {
        ESP_LOGE(TAG, "Invalid CC1101 module: %d", ccModule);
        return false;
    }

    // Check module not busy
    if (CC1101Worker::getState(ccModule) != CC1101State::Idle) {
        ESP_LOGW(TAG, "CC1101 module %d is busy", ccModule);
        return false;
    }

    activeModule_ = ccModule;
    activeFrequency_ = frequency;
    stopRequested_ = false;

    // Reset all decoders
    for (auto& d : decoders_) d->reset();

    // Start CC1101 in ProtoPirate continuous RX mode.
    // This uses a dedicated CC1101State::ProtoPirate so the CC1101Worker
    // does NOT call processRecording (which would steal samples and remove ISR).
    bool started = CC1101Worker::startProtoPirateRX(ccModule, frequency);

    if (!started) {
        ESP_LOGE(TAG, "Failed to start CC1101 ProtoPirate RX on module %d", ccModule);
        return false;
    }

    // Create decode task (statically allocated), pinned to Core 1 (APP core)
    // so its RF sampling stays off Core 0's BT and watchdog-watched IDLE0.
    taskHandle_ = xTaskCreateStaticPinnedToCore(
        decodeTask,
        "PPDecode",
        TASK_STACK_SIZE,
        this,
        2,  // Priority (above idle, below main)
        taskStack_,
        &taskTcb_,
        1  // APP_CPU
    );

    state_ = PPState::Decoding;
    signalCount_ = 0;
    ESP_LOGI(TAG, "Started decoding on module %d at %.2f MHz", ccModule, frequency);

    sendStatus();
    return true;
}

void ProtoPirateModule::stopDecode() {
    if (state_ != PPState::Decoding) return;

    stopRequested_ = true;

    // Wait for task to finish
    if (taskHandle_) {
        // Give the task time to see stopRequested_ and exit
        vTaskDelay(pdMS_TO_TICKS(100));
        taskHandle_ = nullptr;
    }

    // Stop CC1101 ProtoPirate RX
    if (activeModule_ >= 0) {
        CC1101Worker::stopProtoPirateRX(activeModule_);
    }

    state_ = PPState::Idle;
    activeModule_ = -1;
    ESP_LOGI(TAG, "Stopped decoding");

    sendStatus();
}

void ProtoPirateModule::clearHistory() {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        history_.clear();
        xSemaphoreGive(mutex_);
    }
}

void ProtoPirateModule::decodeTask(void* param) {
    auto* self = static_cast<ProtoPirateModule*>(param);
    self->processLoop();
    vTaskDelete(nullptr);
}

void ProtoPirateModule::processLoop() {
    ESP_LOGI(TAG, "Decode task started, monitoring module %d at %.2f MHz",
             activeModule_, activeFrequency_);

    std::vector<unsigned long> localSamples;
    localSamples.reserve(MAX_SAMPLES_BUFFER);

    uint32_t debugTimer = millis();

    while (!stopRequested_) {
        if (activeModule_ < 0) break;

        ReceivedSamples& rxData = CC1101Worker::getSamples(activeModule_);
        portMUX_TYPE& mux = CC1101Worker::getSamplesMux(activeModule_);

        // Read ISR buffer metadata under critical section
        size_t sampleCount = 0;
        unsigned long lastRxTime = 0;

        taskENTER_CRITICAL(&mux);
        sampleCount = rxData.samples.size();
        lastRxTime = rxData.lastReceiveTime;
        taskEXIT_CRITICAL(&mux);

        // Wait for signal completion: gap > MAX_SIGNAL_DURATION (100 ms)
        // This mirrors checkAndSaveRecording() behaviour and ensures the
        // full signal is available before we feed it to decoders.
        bool signalComplete = false;
        if (sampleCount >= 2 && lastRxTime > 0) {
            unsigned long now = micros();
            unsigned long elapsed = now - lastRxTime;
            if (elapsed > MAX_SIGNAL_DURATION) {
                signalComplete = true;
            }
        }

        if (signalComplete) {
            // Copy the complete signal under critical section
            localSamples.clear();
            taskENTER_CRITICAL(&mux);
            localSamples.assign(rxData.samples.begin(), rxData.samples.end());
            rxData.samples.clear();
            rxData.lastReceiveTime = 0;  // Reset for next signal
            taskEXIT_CRITICAL(&mux);

            if (!localSamples.empty()) {
                ++signalCount_;

                // Filter out noise/glitch signals (< 20 samples)
                static constexpr size_t PP_MIN_SAMPLES = 20;
                if (localSamples.size() < PP_MIN_SAMPLES) {
                    ESP_LOGD(TAG, "Signal #%lu discarded: only %zu samples (min %zu)",
                             (unsigned long)signalCount_, localSamples.size(), PP_MIN_SAMPLES);
                } else {
                    // Log first 16 pulse durations for diagnostics
                    char dbg[160];
                    int pos = 0;
                    size_t n = std::min(localSamples.size(), (size_t)16);
                    for (size_t i = 0; i < n && pos < 150; i++) {
                        pos += snprintf(dbg + pos, sizeof(dbg) - pos, "%s%lu",
                                        (i % 2 == 0) ? "+" : "-",
                                        localSamples[i]);
                    }
                    ESP_LOGI(TAG, "Signal #%lu captured: %zu samples on module %d  pulses: %s%s",
                             (unsigned long)signalCount_, localSamples.size(), activeModule_,
                             dbg, localSamples.size() > 16 ? "..." : "");
                    feedSamplesToDecoders(localSamples);
                }
            }
        }

        // Periodic debug status (every 5 s) — also notify app
        if (millis() - debugTimer >= 5000) {
            ESP_LOGI(TAG, "[PP] module %d | pending=%zu | lastRx=%lu | signals=%lu",
                     activeModule_, sampleCount, lastRxTime, (unsigned long)signalCount_);
            sendStatus();
            debugTimer = millis();
        }

        vTaskDelay(pdMS_TO_TICKS(10));  // 10 ms polling interval
    }

    ESP_LOGI(TAG, "Decode task exiting (signals captured: %lu)", (unsigned long)signalCount_);
}

void ProtoPirateModule::feedSamplesToDecoders(const std::vector<unsigned long>& samples) {
    // Samples alternate: HIGH duration, LOW duration, HIGH, LOW, ...
    bool level = true;  // First sample is HIGH
    bool anyDecoded = false;

    for (size_t i = 0; i < samples.size(); i++) {
        uint32_t duration = static_cast<uint32_t>(samples[i]);

        for (auto& d : decoders_) {
            bool decoded = d->feed(level, duration);
            if (decoded) {
                anyDecoded = true;
                PPDecodeResult result = d->getResult();
                result.frequency = activeFrequency_;

                ESP_LOGI(TAG, "Decoded: %s (data=0x%llX, serial=%u, btn=%u)",
                         result.protocolName ? result.protocolName : "?",
                         result.data, result.serial, result.button);

                // Add to history (with dedup)
                if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
                    bool isNew = history_.add(result, (uint32_t)(millis()));
                    xSemaphoreGive(mutex_);

                    if (isNew) {
                        notifyDecodeResult(result);
                    }
                }

                d->reset();
            }
        }

        level = !level;
    }

    // Persist the RAW waveform of every analyzed capture to a .sub so nothing is
    // ever lost — this is the only way an UNRECOGNIZED signal (no decoder, e.g. an
    // FCA/Ram fob) can be saved for later analysis or replay. It announces itself
    // as a recording so the app lists it with no app change.
    std::string savedPath;
    saveRawCapture(samples, savedPath);

    // No decoder recognized the signal — still surface it to the app so the user
    // sees that something WAS captured (e.g. an unsupported make such as a
    // Chrysler/FCA/Ram fob). Carry the pulse count so the UI can show it.
    if (!anyDecoded) {
        PPDecodeResult unknown;
        unknown.protocolName = "Unknown (no decoder)";
        unknown.data        = (uint64_t)samples.size();
        unknown.dataBits    = (uint16_t)std::min(samples.size(), (size_t)0xFFFF);
        unknown.frequency   = activeFrequency_;
        ESP_LOGI(TAG, "Signal unrecognized: %zu pulses, no decoder matched (saved %s)",
                 samples.size(), savedPath.c_str());
        notifyDecodeResult(unknown);
    }
}

bool ProtoPirateModule::saveRawCapture(const std::vector<unsigned long>& samples,
                                       std::string& outPath) {
    outPath.clear();
    if (samples.size() < 2) return false;

    static const char* DIR = "/DATA/SIGNALS";
    if (!SD.exists(DIR)) {
        if (!SD.exists("/DATA")) SD.mkdir("/DATA");
        if (!SD.mkdir(DIR) && !SD.exists(DIR)) {
            ESP_LOGE(TAG, "saveRawCapture: cannot create %s", DIR);
            return false;
        }
    }

    // Filename: pp_<freqKHz>_<counter>.sub (unique per boot).
    // activeFrequency_ is in MHz (e.g. 433.92) -> kHz for a readable name.
    static uint32_t rawSeq = 0;
    uint32_t fkHz = (uint32_t)(activeFrequency_ * 1000.0f);
    char fn[48];
    snprintf(fn, sizeof(fn), "pp_%lu_%04lu.sub",
             (unsigned long)fkHz, (unsigned long)(++rawSeq));
    std::string full = std::string(DIR) + "/" + fn;

    File f = SD.open(full.c_str(), FILE_WRITE);
    if (!f) {
        ESP_LOGE(TAG, "saveRawCapture: cannot open %s", full.c_str());
        return false;
    }

    f.println("Filetype: Flipper SubGhz RAW File");
    f.println("Version: 1");
    f.print("Frequency: ");
    f.print((uint32_t)(activeFrequency_ * 1000000.0f));  // MHz -> Hz
    f.println();
    f.println("Preset: FuriHalSubGhzPresetOok650Async");
    f.println("Protocol: RAW");

    // Samples alternate HIGH, LOW, ... ; write as signed durations (+HIGH, -LOW),
    // 512 values per RAW_Data line (Flipper convention).
    bool level = true;
    int lineCount = 0;
    f.print("RAW_Data: ");
    for (size_t i = 0; i < samples.size(); i++) {
        long v = (long)samples[i];
        f.print(level ? v : -v);
        f.print(' ');
        level = !level;
        if (++lineCount >= 512) {
            f.println();
            f.print("RAW_Data: ");
            lineCount = 0;
        }
    }
    f.println();
    f.close();

    outPath = fn;

    // Announce as a recording (MSG_SIGNAL_RECORDED) so the app lists it with no
    // app change — same path TPMS/record captures already use.
    uint8_t buf[64];
    size_t pos = 0;
    buf[pos++] = MSG_SIGNAL_RECORDED;
    buf[pos++] = (uint8_t)(activeModule_ >= 0 ? activeModule_ : 0);
    uint8_t nl = (uint8_t)strnlen(fn, 40);
    buf[pos++] = nl;
    memcpy(buf + pos, fn, nl);
    pos += nl;
    ClientsManager::getInstance().notifyAllBinary(
        NotificationType::SignalRecorded, buf, pos);

    ESP_LOGI(TAG, "saveRawCapture: wrote %s (%zu pulses)", fn, samples.size());
    return true;
}

void ProtoPirateModule::notifyDecodeResult(const PPDecodeResult& result) {
    // Build binary notification packet for BLE
    // Format: [MSG_PP_DECODE_RESULT][protocolNameLen:1][protocolName...][data:8][data2:8]
    //         [serial:4][button:1][counter:4][dataBits:1][encrypted:1][crcValid:1]
    uint8_t buf[64];
    size_t pos = 0;

    buf[pos++] = MSG_PP_DECODE_RESULT;

    // Protocol name (variable length, max 20 chars)
    const char* name = result.protocolName ? result.protocolName : "Unknown";
    uint8_t nameLen = (uint8_t)strnlen(name, 20);
    buf[pos++] = nameLen;
    memcpy(buf + pos, name, nameLen);
    pos += nameLen;

    // Data (8 bytes LE)
    memcpy(buf + pos, &result.data, 8);
    pos += 8;

    // Data2 (8 bytes LE)
    memcpy(buf + pos, &result.data2, 8);
    pos += 8;

    // Serial (4 bytes LE)
    memcpy(buf + pos, &result.serial, 4);
    pos += 4;

    // Button (1 byte)
    buf[pos++] = result.button;

    // Counter (4 bytes LE)
    memcpy(buf + pos, &result.counter, 4);
    pos += 4;

    // dataBits (1 byte)
    buf[pos++] = result.dataBits;

    // encrypted (1 byte)
    buf[pos++] = result.encrypted ? 1 : 0;

    // crcValid (1 byte)
    buf[pos++] = result.crcValid ? 1 : 0;

    ClientsManager::getInstance().notifyAllBinary(
        NotificationType::SignalDetected, buf, pos);
}

bool ProtoPirateModule::sendHistoryEntry(int index) {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;

    const PPHistoryEntry* entry = history_.get(index);
    if (!entry) {
        xSemaphoreGive(mutex_);
        return false;
    }

    // Copy result while holding mutex
    PPDecodeResult result = entry->result;
    uint32_t ts = entry->timestampMs;
    xSemaphoreGive(mutex_);

    // Build BLE packet — same format as decode result but with index + timestamp
    uint8_t buf[72];
    size_t pos = 0;

    buf[pos++] = MSG_PP_HISTORY_ENTRY;
    buf[pos++] = (uint8_t)index;

    // Timestamp (4 bytes LE)
    memcpy(buf + pos, &ts, 4);
    pos += 4;

    // Protocol name
    const char* name = result.protocolName ? result.protocolName : "Unknown";
    uint8_t nameLen = (uint8_t)strnlen(name, 20);
    buf[pos++] = nameLen;
    memcpy(buf + pos, name, nameLen);
    pos += nameLen;

    // Data fields (same as notifyDecodeResult)
    memcpy(buf + pos, &result.data, 8);   pos += 8;
    memcpy(buf + pos, &result.data2, 8);  pos += 8;
    memcpy(buf + pos, &result.serial, 4); pos += 4;
    buf[pos++] = result.button;
    memcpy(buf + pos, &result.counter, 4); pos += 4;
    buf[pos++] = result.dataBits;
    buf[pos++] = result.encrypted ? 1 : 0;
    buf[pos++] = result.crcValid ? 1 : 0;

    ClientsManager::getInstance().notifyAllBinary(
        NotificationType::SignalDetected, buf, pos);
    return true;
}

void ProtoPirateModule::sendHistoryCount() {
    uint8_t buf[3];
    buf[0] = MSG_PP_HISTORY_COUNT;
    uint16_t count = (uint16_t)history_.getCount();
    memcpy(buf + 1, &count, 2);
    ClientsManager::getInstance().notifyAllBinary(
        NotificationType::SignalDetected, buf, 3);
}

void ProtoPirateModule::sendStatus() {
    // Format: [0xB7][state:1][module:1][freqx100:2LE][signalCount:4LE] = 9 bytes
    uint8_t buf[9];
    buf[0] = MSG_PP_STATUS;
    buf[1] = (uint8_t)state_;
    buf[2] = (activeModule_ >= 0) ? (uint8_t)activeModule_ : 0xFF;
    // Frequency as uint16 in 10kHz units (e.g. 433.92 → 43392)
    uint16_t freqVal = (uint16_t)(activeFrequency_ * 100);
    memcpy(buf + 3, &freqVal, 2);
    // Signal count (total RF signals analyzed in this session)
    uint32_t sc = signalCount_;
    memcpy(buf + 5, &sc, 4);
    ClientsManager::getInstance().notifyAllBinary(
        NotificationType::SignalDetected, buf, 9);
}

bool ProtoPirateModule::loadSubFile(const char* filePath) {
    if (!mutex_) {
        ESP_LOGE(TAG, "loadSubFile: not initialized");
        return false;
    }

    // Reset all decoders before feeding file data
    for (auto& d : decoders_) d->reset();

    StreamingSubFileParser parser;
    StreamingSubFileParser::SubFileHeader header;

    if (!parser.parseHeader(filePath, header)) {
        ESP_LOGE(TAG, "loadSubFile: failed to parse header of %s", filePath);
        return false;
    }

    ESP_LOGI(TAG, "loadSubFile: %s  freq=%u Hz  preset=%s",
             filePath, header.frequency, header.preset.c_str());

    // Set active frequency from file header (Hz → MHz)
    // so decoded results carry the correct frequency value.
    if (header.frequency > 0) {
        activeFrequency_ = (float)header.frequency / 1000000.0f;
    }

    // Collect samples from the .sub file, but CAP the count: a big RAW sweep
    // (e.g. a 100 KB Tesla file ≈ 25k pulses) would grow this vector past the
    // free heap and throw bad_alloc. Decoders identify a protocol from the
    // start of the signal, so the first MAX_LOAD_SAMPLES pulses are plenty.
    static constexpr size_t MAX_LOAD_SAMPLES = 8192;  // 32 KB, well within heap
    std::vector<unsigned long> samples;
    samples.reserve(MAX_LOAD_SAMPLES);
    size_t totalPulses = 0;
    bool capped = false;

    parser.streamRawData(filePath, [&](int32_t duration_us, bool pinState) {
        // streamRawData emits SIGNED durations (+HIGH, -LOW). Store the ABSOLUTE
        // value of EVERY pulse so the HIGH/LOW alternation is preserved by position
        // (even=HIGH, odd=LOW), exactly as feedSamplesToDecoders expects.
        // BUGFIX: the old code did `if (duration_us > 0)`, which DROPPED every LOW
        // pulse and fed the decoders a mangled half-signal — so Load-.sub never
        // actually decoded anything.
        (void)pinState;
        if (samples.size() < MAX_LOAD_SAMPLES) {
            uint32_t mag = (duration_us < 0) ? (uint32_t)(-duration_us)
                                             : (uint32_t)duration_us;
            if (mag > 0) samples.push_back((unsigned long)mag);
        } else {
            capped = true;
        }
        totalPulses++;
    });
    if (capped) {
        ESP_LOGW(TAG, "loadSubFile: %s has %zu pulses; capped at %zu for decode",
                 filePath, totalPulses, MAX_LOAD_SAMPLES);
    }

    if (samples.size() < 20) {
        ESP_LOGW(TAG, "loadSubFile: too few samples (%zu) in %s", samples.size(), filePath);
        return false;
    }

    // Log first 16 pulses for diagnostics
    char dbg[160];
    int pos = 0;
    size_t n = std::min(samples.size(), (size_t)16);
    for (size_t i = 0; i < n && pos < 150; i++) {
        pos += snprintf(dbg + pos, sizeof(dbg) - pos, "%s%lu",
                        (i % 2 == 0) ? "+" : "-",
                        samples[i]);
    }
    ESP_LOGI(TAG, "loadSubFile: %zu samples, pulses: %s%s",
             samples.size(), dbg, samples.size() > 16 ? "..." : "");

    // Feed to decoders exactly as we do for ISR-captured signals
    feedSamplesToDecoders(samples);

    // Check if any decoder found a match (getResult has valid data)
    // We look for results that were already notified during feedSamplesToDecoders
    ESP_LOGI(TAG, "loadSubFile: analysis complete for %s (%zu pulses processed)",
             filePath, samples.size());

    return true;
}

// ── Emulate (TX) a decoded result ──────────────────────────────

PPProtocol* ProtoPirateModule::findProtocolByName(const char* name) {
    if (!name) return nullptr;
    for (auto& d : decoders_) {
        if (strcmp(d->getName(), name) == 0) {
            return d.get();
        }
    }
    return nullptr;
}

bool ProtoPirateModule::emulate(const PPDecodeResult& result,
                                 int ccModule, int repeatCount) {
    if (!mutex_) {
        ESP_LOGE(TAG, "emulate: not initialized");
        notifyTxStatus(3, 1);  // error: not initialized
        return false;
    }

    if (ccModule < 0 || ccModule >= CC1101_NUM_MODULES) {
        ESP_LOGE(TAG, "emulate: invalid module %d", ccModule);
        notifyTxStatus(3, 2);  // error: bad module
        return false;
    }

    // Find protocol encoder
    PPProtocol* proto = findProtocolByName(result.protocolName);
    if (!proto) {
        ESP_LOGE(TAG, "emulate: unknown protocol '%s'",
                 result.protocolName ? result.protocolName : "null");
        notifyTxStatus(3, 3);  // error: unknown protocol
        return false;
    }

    if (!proto->canEmulate()) {
        ESP_LOGW(TAG, "emulate: protocol '%s' does not support TX", proto->getName());
        notifyTxStatus(3, 4);  // error: protocol cannot emulate
        return false;
    }

    // Generate pulse data from the protocol encoder
    std::vector<PPPulse> pulses = proto->generatePulseData(result);
    if (pulses.empty()) {
        ESP_LOGE(TAG, "emulate: generatePulseData returned empty for '%s'", proto->getName());
        notifyTxStatus(3, 5);  // error: empty pulse data
        return false;
    }

    ESP_LOGI(TAG, "emulate: %s, %zu pulses, %d repeats on module %d",
             result.protocolName, pulses.size(), repeatCount, ccModule);

    // If currently decoding on this module, stop first
    bool wasDecoding = (state_ == PPState::Decoding && activeModule_ == ccModule);
    if (wasDecoding) {
        ESP_LOGI(TAG, "emulate: pausing decode on module %d for TX", ccModule);
        stopDecode();
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Check module is idle (might be used by another feature)
    if (CC1101Worker::getState(ccModule) != CC1101State::Idle) {
        // Try to go idle
        CC1101Worker::goIdle(ccModule);
        vTaskDelay(pdMS_TO_TICKS(100));
        if (CC1101Worker::getState(ccModule) != CC1101State::Idle) {
            ESP_LOGE(TAG, "emulate: module %d not idle after goIdle", ccModule);
            notifyTxStatus(3, 6);  // error: module busy
            return false;
        }
    }

    notifyTxStatus(1);  // transmitting

    // Determine frequency in Hz and configure CC1101 for TX
    float freqMHz = (result.frequency > 0) ? result.frequency : activeFrequency_;
    if (freqMHz <= 0) freqMHz = 433.92f;

    // Use external CC1101 module references for TX config
    // (moduleCC1101State is declared extern in CC1101_Module.h,
    //  already included via CC1101_Worker.h → CC1101_Module.h)
    moduleCC1101State[ccModule].backupConfig()
        .setTransmitConfig(freqMHz, MODULATION_ASK_OOK, 0)
        .initConfig();

    vTaskDelay(pdMS_TO_TICKS(10));

    int outputPin = moduleCC1101State[ccModule].getOutputPin();

    // Transmit the pulses (blocking, with repeats)
    for (int rep = 0; rep < repeatCount; rep++) {
        for (auto& p : pulses) {
            if (p.duration > 0) {
                digitalWrite(outputPin, HIGH);
                delayMicroseconds((uint32_t)p.duration);
            } else if (p.duration < 0) {
                digitalWrite(outputPin, LOW);
                delayMicroseconds((uint32_t)(-p.duration));
            }
        }
        // Ensure pin goes LOW between repeats
        digitalWrite(outputPin, LOW);
        if (rep < repeatCount - 1) {
            delayMicroseconds(10000);  // 10 ms gap between repeats
        }
        taskYIELD();
    }

    // Restore CC1101 config
    moduleCC1101State[ccModule].restoreConfig();
    moduleCC1101State[ccModule].setSidle();

    ESP_LOGI(TAG, "emulate: TX complete for '%s' (%d repeats)", result.protocolName, repeatCount);
    notifyTxStatus(2);  // done

    // Restart decoding if it was active before
    if (wasDecoding) {
        ESP_LOGI(TAG, "emulate: restarting decode on module %d at %.2f MHz",
                 ccModule, freqMHz);
        vTaskDelay(pdMS_TO_TICKS(100));
        startDecode(ccModule, freqMHz);
    }

    return true;
}

// ── Emulate the NEXT rolling code (authorized testing) ──────────

bool ProtoPirateModule::emulateForward(const PPDecodeResult& in, int ccModule,
                                        int counterDelta, int repeatCount) {
    PPProtocol* proto = findProtocolByName(in.protocolName);
    if (!proto) {
        ESP_LOGE(TAG, "emulateForward: unknown protocol '%s'",
                 in.protocolName ? in.protocolName : "null");
        notifyTxStatus(3, 3);
        return false;
    }
    if (!proto->supportsForward()) {
        ESP_LOGW(TAG, "emulateForward: '%s' has no forward-code support",
                 proto->getName());
        notifyTxStatus(3, 7);  // error: protocol can't roll forward
        return false;
    }
    if (counterDelta < 1) counterDelta = 1;

    PPDecodeResult fwd;
    if (!proto->generateForward(in, counterDelta, fwd)) {
        // Almost always: the manufacturer key is not in the keystore.
        ESP_LOGW(TAG, "emulateForward: no key / cannot roll forward for '%s' "
                      "(load the manufacturer key into the keystore)",
                 proto->getName());
        notifyTxStatus(3, 8);  // error: no key in store
        return false;
    }

    ESP_LOGI(TAG, "emulateForward: %s counter %u -> %u (delta %d), TX on module %d",
             in.protocolName, (unsigned)in.counter, (unsigned)fwd.counter,
             counterDelta, ccModule);
    return emulate(fwd, ccModule, repeatCount);
}

// ── Load manufacturer keys from SD into the shared keystore ─────

int ProtoPirateModule::loadKeystoreFromSd(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) {
        ESP_LOGW(TAG, "keystore: no key file at %s (keystore stays empty)", path);
        return -1;
    }
    int loaded = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0 || line[0] == '#') continue;

        // name:learning:hexkey
        int c1 = line.indexOf(':');
        int c2 = (c1 >= 0) ? line.indexOf(':', c1 + 1) : -1;
        if (c1 <= 0 || c2 <= c1) continue;

        String name = line.substring(0, c1);
        String learn = line.substring(c1 + 1, c2);
        String hex = line.substring(c2 + 1);
        name.trim(); learn.trim(); hex.trim();
        if (hex.startsWith("0x") || hex.startsWith("0X")) hex = hex.substring(2);
        if (hex.length() == 0) continue;

        uint64_t key = strtoull(hex.c_str(), nullptr, 16);
        uint8_t learning = pp::Keystore::parseLearning(learn.c_str());
        if (pp::Keystore::add(name.c_str(), key, learning)) loaded++;
    }
    f.close();
    ESP_LOGI(TAG, "keystore: loaded %d key(s) from %s (total %u)",
             loaded, path, (unsigned)pp::Keystore::count());
    return loaded;
}

// ── On-device forward-code self-test (synthetic key, no RF) ─────

bool ProtoPirateModule::forwardSelfTest(char* msg, size_t msgLen) {
    bool allOk = true;
    char star[48] = "StarLine SKIP";
    char fiat[48] = "Fiat SKIP";

    // ---- StarLine (KeeLoq, keystore) ----
    {
        const uint64_t K = 0x0123456789ABCDEFULL;  // synthetic device key (simple)
        const uint8_t  btn = 0x2;
        const uint32_t serial = 0xA5B6C7;
        const uint16_t cnt = 0x0041;
        uint32_t keyFix = ((uint32_t)btn << 24) | (serial & 0xFFFFFF);
        uint8_t  endSer = keyFix & 0xFF;
        uint32_t plain  = ((uint32_t)btn << 24) | ((uint32_t)endSer << 16) | cnt;
        uint32_t hop    = keeloq::encrypt(plain, K);
        uint64_t key64  = ((uint64_t)keyFix << 32) | hop;

        size_t saved = pp::Keystore::count();
        pp::Keystore::add("__selftest__", K, 1 /*simple*/);

        PPDecodeResult in;
        memset(&in, 0, sizeof(in));
        in.protocolName = PPStarLine::PROTOCOL_NAME;
        in.data = pp_reverse64(key64);
        in.counter = cnt; in.dataBits = 64;

        bool ok = false; uint32_t fwdCnt = 0;
        PPProtocol* proto = findProtocolByName(in.protocolName);
        if (proto && proto->supportsForward()) {
            PPDecodeResult out;
            if (proto->generateForward(in, 1, out)) {
                fwdCnt = out.counter;
                uint32_t kh = (uint32_t)(pp_reverse64(out.data) & 0xFFFFFFFF);
                uint32_t d  = keeloq::decrypt(kh, K);
                bool valid  = ((d >> 24) == btn) && (((d >> 16) & 0xFF) == endSer);
                ok = valid && ((d & 0xFFFF) == (uint32_t)(cnt + 1)) &&
                     (out.data != in.data);
            }
        }
        pp::Keystore::truncate(saved);
        snprintf(star, sizeof(star), "StarLine %u->%u %s",
                 (unsigned)cnt, (unsigned)fwdCnt, ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    // ---- Fiat V0 (cleartext counter) ----
    {
        const uint32_t serial = 0x11223344;
        const uint32_t cnt = 0x0000ABCD;
        PPDecodeResult in;
        memset(&in, 0, sizeof(in));
        in.protocolName = PPFiatV0::PROTOCOL_NAME;
        in.data = ((uint64_t)cnt << 32) | serial;
        in.counter = cnt; in.dataBits = 64;

        bool ok = false; uint32_t fwdCnt = 0;
        PPProtocol* proto = findProtocolByName(in.protocolName);
        if (proto && proto->supportsForward()) {
            PPDecodeResult out;
            if (proto->generateForward(in, 1, out)) {
                fwdCnt = out.counter;
                ok = (out.counter == cnt + 1) &&
                     ((uint32_t)(out.data >> 32) == cnt + 1) &&
                     ((uint32_t)(out.data & 0xFFFFFFFF) == serial);
            }
        }
        snprintf(fiat, sizeof(fiat), "Fiat %u->%u %s",
                 (unsigned)cnt, (unsigned)fwdCnt, ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    char kia[48] = "Kia SKIP";
    // ---- Kia V3/V4 (KeeLoq, keystore) ----
    {
        const uint64_t K = 0x1122334455667788ULL;
        const uint8_t  btn = 0x2;
        const uint16_t cnt = 0x1000;
        uint32_t plain = ((uint32_t)btn << 28) | cnt;      // middle bits 0
        uint32_t enc   = keeloq::encrypt(plain, K);
        uint8_t b[8];
        b[0] = pp_reverse8((uint8_t)(enc & 0xFF));
        b[1] = pp_reverse8((uint8_t)((enc >> 8) & 0xFF));
        b[2] = pp_reverse8((uint8_t)((enc >> 16) & 0xFF));
        b[3] = pp_reverse8((uint8_t)((enc >> 24) & 0xFF));
        b[4] = 0; b[5] = 0; b[6] = 0;
        b[7] = pp_reverse8((uint8_t)(btn << 4));            // top nibble = btn
        uint64_t data = 0;
        for (int k = 0; k < 8; k++) data |= (uint64_t)b[k] << (56 - 8 * k);

        size_t saved = pp::Keystore::count();
        pp::Keystore::add("__selftest__", K, 1);

        PPDecodeResult in;
        memset(&in, 0, sizeof(in));
        in.protocolName = PPKiaV3V4::PROTOCOL_NAME;
        in.data = data; in.counter = cnt; in.dataBits = 68;
        in.type = "V4";

        bool ok = false; uint32_t fwdCnt = 0;
        PPProtocol* proto = findProtocolByName(in.protocolName);
        if (proto && proto->supportsForward()) {
            PPDecodeResult out;
            if (proto->generateForward(in, 1, out)) {
                fwdCnt = out.counter;
                uint8_t ob[8];
                for (int k = 0; k < 8; k++) ob[k] = (uint8_t)(out.data >> (56 - 8 * k));
                uint32_t oenc = ((uint32_t)pp_reverse8(ob[3]) << 24) |
                                ((uint32_t)pp_reverse8(ob[2]) << 16) |
                                ((uint32_t)pp_reverse8(ob[1]) << 8) |
                                pp_reverse8(ob[0]);
                uint32_t op = keeloq::decrypt(oenc, K);
                ok = (((op >> 28) & 0x0F) == btn) &&
                     ((op & 0xFFFF) == (uint32_t)(cnt + 1)) &&
                     (out.data != in.data);

                // Encode-roundtrip: the TX encoder I wrote must produce a frame
                // our own decoder reads back to the same forward counter.
                if (ok) {
                    PPKiaV3V4 dec;
                    bool rtOk = false;
                    auto pulses = proto->generatePulseData(out);
                    for (auto& p : pulses) {
                        bool lvl = p.duration > 0;
                        uint32_t du = (uint32_t)(lvl ? p.duration : -p.duration);
                        if (dec.feed(lvl, du)) {
                            rtOk = (dec.getResult().counter == (uint32_t)(cnt + 1));
                            break;
                        }
                    }
                    ok = ok && rtOk;
                }
            }
        }
        pp::Keystore::truncate(saved);
        snprintf(kia, sizeof(kia), "Kia %u->%u %s",
                 (unsigned)cnt, (unsigned)fwdCnt, ok ? "PASS" : "FAIL");
        allOk = allOk && ok;
    }

    char ford[52] = "Ford SKIP";
    // ---- Ford V0: full RF roundtrip (forward -> ported encoder -> re-ported
    // decoder). Encoder + decoder now match the upstream format exactly. ----
    {
        PPProtocol* proto = findProtocolByName(PPFordV0::PROTOCOL_NAME);
        if (proto && proto->supportsForward()) {
            auto feedTo = [](PPFordV0& d, const std::vector<PPPulse>& ps) -> bool {
                for (auto& p : ps) {
                    bool lvl = p.duration > 0;
                    uint32_t du = (uint32_t)(lvl ? p.duration : -p.duration);
                    if (d.feed(lvl, du)) return true;
                }
                return false;
            };
            uint64_t key1 = 0x0123456789ABCDEULL;
            uint16_t key2 = 0x55AA;
            uint32_t ser0 = 0, cnt0 = 0; uint8_t btn0 = 0; bool crc0 = false;
            PPFordV0::decodeFields(key1, key2, ser0, btn0, cnt0, crc0);

            PPDecodeResult in;
            memset(&in, 0, sizeof(in));
            in.protocolName = PPFordV0::PROTOCOL_NAME;
            in.data = key1; in.data2 = key2;
            in.counter = cnt0; in.button = btn0; in.serial = ser0;

            bool ok = false; uint32_t cnt1 = 0;
            PPDecodeResult out;
            if (proto->generateForward(in, 1, out)) {
                PPFordV0 dec;
                if (feedTo(dec, proto->generatePulseData(out))) {
                    PPDecodeResult fwd = dec.getResult();
                    cnt1 = fwd.counter;
                    ok = (fwd.counter == cnt0 + 1) && (fwd.button == btn0) &&
                         (fwd.serial == ser0) && fwd.crcValid;
                    snprintf(ford, sizeof(ford), "Ford %u->%u %s",
                             (unsigned)cnt0, (unsigned)cnt1, ok ? "PASS" : "FAIL");
                } else {
                    snprintf(ford, sizeof(ford), "Ford FAIL(rf-decode)");
                }
            } else {
                snprintf(ford, sizeof(ford), "Ford FAIL(gen)");
            }
            allOk = allOk && ok;
        }
    }

    char vag[64] = "VAG SKIP";
    // ---- VAG forward (data-level): type 2 (XTEA, built-in key) and an AUT64
    // type (synthetic key). Both: build frame -> forward(+1) -> re-decode. ----
    {
        PPProtocol* proto = findProtocolByName(PPVag::PROTOCOL_NAME);
        if (proto && proto->supportsForward()) {
            auto testVag = [&](uint8_t keyIndex, uint8_t typeByte,
                               uint8_t dispatch, uint32_t& c0, uint32_t& c1) -> bool {
                uint8_t dec[8] = {0x11, 0x22, 0x33, 0x44, 0x34, 0x12, 0x00, 0x20};
                c0 = (uint32_t)dec[4] | ((uint32_t)dec[5] << 8) | ((uint32_t)dec[6] << 16);
                uint16_t key2 = 0;
                uint64_t key1 = PPVag::encodeBlock(dec, typeByte, dispatch, keyIndex, key2);
                PPDecodeResult in;
                memset(&in, 0, sizeof(in));
                in.protocolName = PPVag::PROTOCOL_NAME;
                in.data = key1; in.data2 = key2; in.counter = c0;
                in.keyIndex = keyIndex;
                PPDecodeResult out;
                if (!proto->generateForward(in, 1, out)) { c1 = 0; return false; }
                c1 = PPVag::decodeBlockCnt(out.data, (uint16_t)out.data2, keyIndex);
                return (c1 == c0 + 1) && (out.data != in.data);
            };

            // Type 2 (XTEA, built-in key)
            uint32_t t2c0 = 0, t2c1 = 0;
            bool t2 = testVag(0xFF, 0xC0, 0x2A, t2c0, t2c1);

            // AUT64 type (synthetic identity-box key in slot 0)
            aut64::Key sk;
            for (int i = 0; i < 8; i++) { sk.key[i] = (uint8_t)(0x11 * (i + 1)); sk.pbox[i] = (uint8_t)i; }
            for (int i = 0; i < 16; i++) sk.sbox[i] = (uint8_t)i;
            PPVag::setAut64Key(0, sk);
            uint32_t a0 = 0, a1 = 0;
            bool au = testVag(0, 0xC0, 0x2B, a0, a1);

            snprintf(vag, sizeof(vag), "VAG t2 %u->%u %s / aut64 %u->%u %s",
                     (unsigned)t2c0, (unsigned)t2c1, t2 ? "PASS" : "FAIL",
                     (unsigned)a0, (unsigned)a1, au ? "PASS" : "FAIL");
            allOk = allOk && t2 && au;
        }
    }

    snprintf(msg, msgLen, "fwd self-test: %s | %s | %s | %s | %s | %s",
             star, fiat, kia, ford, vag, allOk ? "ALL PASS" : "FAIL");
    ESP_LOGI(TAG, "%s", msg);
    return allOk;
}

// ── Save decoded result to SD card ──────────────────────────────

bool ProtoPirateModule::saveCapture(const PPDecodeResult& result,
                                     std::string& outPath) {
    if (!mutex_) {
        ESP_LOGE(TAG, "saveCapture: not initialized");
        notifySaveResult(false, "");
        return false;
    }

    // Find protocol encoder
    PPProtocol* proto = findProtocolByName(result.protocolName);
    if (!proto) {
        ESP_LOGE(TAG, "saveCapture: unknown protocol '%s'",
                 result.protocolName ? result.protocolName : "null");
        notifySaveResult(false, "");
        return false;
    }

    // Generate pulse data
    std::vector<PPPulse> pulses = proto->generatePulseData(result);
    if (pulses.empty()) {
        ESP_LOGW(TAG, "saveCapture: empty pulse data, saving metadata only");
    }

    // Ensure /DATA/PROTOPIRATE/ directory exists
    static const char* PP_DIR = "/DATA/PROTOPIRATE";
    if (!SD.exists(PP_DIR)) {
        if (!SD.mkdir(PP_DIR)) {
            // Try creating parent first
            if (!SD.exists("/DATA")) SD.mkdir("/DATA");
            if (!SD.mkdir(PP_DIR)) {
                ESP_LOGE(TAG, "saveCapture: failed to create %s", PP_DIR);
                notifySaveResult(false, "");
                return false;
            }
        }
        ESP_LOGI(TAG, "saveCapture: created directory %s", PP_DIR);
    }

    // Generate filename: ProtoName_Serial_NNNN.sub
    // Replace spaces/slashes in protocol name
    char safeName[32];
    size_t ni = 0;
    const char* pn = result.protocolName ? result.protocolName : "Unknown";
    for (size_t i = 0; pn[i] && ni < sizeof(safeName) - 1; i++) {
        char c = pn[i];
        if (c == ' ' || c == '/' || c == '\\') c = '_';
        safeName[ni++] = c;
    }
    safeName[ni] = '\0';

    // Find next available file number
    char filePath[128];
    int fileNum = 1;
    for (; fileNum <= 9999; fileNum++) {
        snprintf(filePath, sizeof(filePath), "%s/%s_%04d.sub", PP_DIR, safeName, fileNum);
        if (!SD.exists(filePath)) break;
    }
    if (fileNum > 9999) {
        ESP_LOGE(TAG, "saveCapture: too many files for protocol '%s'", safeName);
        notifySaveResult(false, "");
        return false;
    }

    // Convert PPPulse vector to unsigned long vector for FlipperSubFile
    std::vector<unsigned long> samples;
    samples.reserve(pulses.size());
    for (auto& p : pulses) {
        samples.push_back((unsigned long)std::abs(p.duration));
    }

    // Determine frequency
    float freqMHz = (result.frequency > 0) ? result.frequency : activeFrequency_;
    if (freqMHz <= 0) freqMHz = 433.92f;

    // Determine preset from protocol
    const char* presetName = result.presetName ? result.presetName : "Ook650";

    // Write .sub file
    File file = SD.open(filePath, FILE_WRITE);
    if (!file) {
        ESP_LOGE(TAG, "saveCapture: failed to open %s for writing", filePath);
        notifySaveResult(false, "");
        return false;
    }

    std::vector<byte> emptyCustom;
    FlipperSubFile::generateRaw(file, std::string(presetName), emptyCustom, samples, freqMHz);
    file.close();

    outPath = filePath;
    ESP_LOGI(TAG, "saveCapture: saved to %s (%zu samples)", filePath, samples.size());
    notifySaveResult(true, filePath);
    return true;
}

// ── TX/Save notification helpers ────────────────────────────────

void ProtoPirateModule::notifyTxStatus(uint8_t state, uint8_t errCode) {
    uint8_t buf[3];
    buf[0] = MSG_PP_TX_STATUS;
    buf[1] = state;
    buf[2] = errCode;
    ClientsManager::getInstance().notifyAllBinary(
        NotificationType::SignalDetected, buf, 3);
}

void ProtoPirateModule::notifySaveResult(bool success, const char* path) {
    uint8_t buf[130];
    size_t pos = 0;
    buf[pos++] = MSG_PP_SAVE_RESULT;
    buf[pos++] = success ? 1 : 0;
    uint8_t pathLen = path ? (uint8_t)std::min(strlen(path), (size_t)127) : 0;
    buf[pos++] = pathLen;
    if (pathLen > 0) {
        memcpy(buf + pos, path, pathLen);
        pos += pathLen;
    }
    ClientsManager::getInstance().notifyAllBinary(
        NotificationType::SignalDetected, buf, pos);
}
