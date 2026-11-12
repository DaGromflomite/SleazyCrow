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
#include "esp_log.h"

class TpmsCommands {
public:
    static void registerCommands(CommandHandler& handler) {
        handler.registerCommand(0x70, handleStart);
        handler.registerCommand(0x71, handleStop);
        handler.registerCommand(0x72, handleStatus);
    }

private:
    static bool handleStart(const uint8_t* data, size_t len) {
        int module = 0;
        float freq = 315.0f;
        if (len >= 1) module = data[0] ? 1 : 0;
        if (len >= 3) {
            uint16_t fx100 = (uint16_t)(data[1] | (data[2] << 8));
            if (fx100 >= 28000 && fx100 <= 93000) freq = fx100 / 100.0f;
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
};

#endif // TPMS_COMMANDS_H
