/**
 * @file TpmsCommands.h
 * @brief BLE command handlers for the TPMS sniffer (vehicle stalking detection).
 *
 * Command protocol (App -> Device):
 *   0x70 = TPMS_START  — Start sniffing. Optional payload [module:1][freqx100:2LE]
 *                        (defaults: module 0, 315.00 MHz US). 43392 = 433.92 EU.
 *   0x71 = TPMS_STOP   — Stop sniffing, free the CC1101.
 *   0x72 = TPMS_STATUS — Request current status.
 *
 * Device -> App: MSG_TPMS_HIT (0xD8) per decoded sensor, MSG_TPMS_STATUS (0xD9).
 * Cross-trip "same sensor = follower" tracking is done app-side.
 */
#ifndef TPMS_COMMANDS_H
#define TPMS_COMMANDS_H

#include <Arduino.h>
#include "core/ble/CommandHandler.h"
#include "core/ble/ClientsManager.h"
#include "BinaryMessages.h"
#include "modules/tpms/TpmsModule.h"
#include "modules/tpms/TpmsDecoders.h"
#include "esp_log.h"

class TpmsCommands {
public:
    static void registerCommands(CommandHandler& handler) {
        handler.registerCommand(0x70, handleStart);
        handler.registerCommand(0x71, handleStop);
        handler.registerCommand(0x72, handleStatus);
        handler.registerCommand(0x73, handleSelfTest);
    }

private:
    static bool handleStart(const uint8_t* data, size_t len) {
        int module = 0;
        float freq = 315.0f;
        if (len >= 1) module = data[0] ? 1 : 0;
        if (len >= 3) {
            uint16_t fx100 = (uint16_t)(data[1] | (data[2] << 8));
            if (fx100 >= 28000 && fx100 <= 47000) freq = fx100 / 100.0f;
        }
        bool ok = TpmsModule::instance().start(module, freq);
        uint8_t resp[] = { (uint8_t)(ok ? MSG_COMMAND_SUCCESS : MSG_COMMAND_ERROR), 0 };
        ClientsManager::getInstance().notifyAllBinary(
            NotificationType::TpmsEvent, resp, sizeof(resp));
        return ok;
    }

    static bool handleStop(const uint8_t* data, size_t len) {
        TpmsModule::instance().stop();
        uint8_t resp[] = { MSG_COMMAND_SUCCESS, 0 };
        ClientsManager::getInstance().notifyAllBinary(
            NotificationType::TpmsEvent, resp, sizeof(resp));
        return true;
    }

    static bool handleStatus(const uint8_t* data, size_t len) {
        TpmsModule::instance().sendStatus();
        return true;
    }

    // 0x73 — run the TPMS self-test on the real device.
    // Reply: [MSG_COMMAND_SUCCESS][mask]
    //   bit0 = Ford byte-decoder, bit1 = Toyota byte-decoder,
    //   bit2 = full demod->decode chain (synthetic Ford pulse train). 0x07 = all.
    static bool handleSelfTest(const uint8_t* data, size_t len) {
        uint8_t mask = tpms::selfTest();          // bit0 Ford, bit1 Toyota
        if (TpmsModule::demodSelfTest()) mask |= 0x04;
        uint8_t resp[] = { MSG_COMMAND_SUCCESS, mask };
        ClientsManager::getInstance().notifyAllBinary(
            NotificationType::TpmsEvent, resp, sizeof(resp));
        ESP_LOGI("Tpms", "self-test mask=0x%02X (b0=Ford b1=Toyota b2=demod)", mask);

        // Also emit the decoded Ford vector as a real TPMS hit, so the app can
        // show exactly how a decoded sensor displays (a live demo + diagnostic).
        const uint8_t v[8] = {0x12, 0x34, 0x56, 0x78, 0x7C, 0x47, 0x04, 0xDB};
        TpmsDecoded d;
        if (tpms::decodeFord(v, 8, d)) {
            BinaryTpmsHit hit;
            hit.sensorId = d.id;
            hit.pressure_kpa = d.pressureKpa;
            hit.temperature_c = d.tempC;
            hit.rssi = -42;
            hit.protocol = d.protocol;
            hit.flags = 0x01;
            ClientsManager::getInstance().notifyAllBinary(
                NotificationType::TpmsEvent, (const uint8_t*)&hit, sizeof(hit));
        }
        return (mask == 0x07);
    }
};

#endif // TPMS_COMMANDS_H
