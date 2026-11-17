#pragma once
/**
 * @file PPKeystore.h
 * @brief Central manufacturer-key store for ProtoPirate rolling-code protocols.
 *
 * AUTHORIZED SECURITY TESTING ONLY. This store holds the manufacturer /
 * learning keys that rolling-code decoders use to recover a fob's counter and
 * that the forward-code generator uses to synthesize the next valid code.
 *
 * Keys are NOT hard-coded here. They are loaded at runtime from:
 *   1. an SD-card file  (/DATA/KEYS/keeloq_mfcodes.txt)  — tester-supplied, and
 *   2. the BLE "add key" command                         — tester-supplied.
 *
 * File format (one key per line, '#' comments allowed):
 *     <name>:<learning>:<16-hex-key>
 *   where <learning> is one of:
 *     simple | normal | magic_xor1 | magic_serial1 | magic_serial2 |
 *     magic_serial3 | unknown        (or the numeric keeloq::LearningType)
 *   e.g.  MyAlarm:normal:0123456789ABCDEF
 *
 * All protocols share this one store, so a key added once is tried by every
 * rolling-code decoder (StarLine, KIA, Subaru, VAG, PSA, ...).
 */

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace pp {

/// One manufacturer / learning key.
struct MfKey {
    char     name[24];
    uint64_t key;
    uint8_t  learning;   ///< keeloq::LearningType (0=unknown,1=simple,2=normal,...)
};

class Keystore {
public:
    static constexpr size_t MAX_KEYS = 64;

    /// Add (or append) a key. Returns false if the store is full.
    static bool add(const char* name, uint64_t key, uint8_t learning) {
        if (count_ >= MAX_KEYS) return false;
        MfKey& k = keys_[count_];
        std::memset(k.name, 0, sizeof(k.name));
        if (name) std::strncpy(k.name, name, sizeof(k.name) - 1);
        k.key = key;
        k.learning = learning;
        count_++;
        return true;
    }

    static void clear() { count_ = 0; }
    static size_t count() { return count_; }

    /// Truncate the store back to `n` entries (used to undo a temporary add).
    static void truncate(size_t n) { if (n < count_) count_ = n; }

    /// Get key i (nullptr if out of range).
    static const MfKey* get(size_t i) {
        return (i < count_) ? &keys_[i] : nullptr;
    }

    /// Find a key by name (nullptr if absent).
    static const MfKey* findByName(const char* name) {
        if (!name) return nullptr;
        for (size_t i = 0; i < count_; i++) {
            if (std::strcmp(keys_[i].name, name) == 0) return &keys_[i];
        }
        return nullptr;
    }

    /// Parse a learning-type token (word or number) → keeloq::LearningType value.
    static uint8_t parseLearning(const char* tok) {
        if (!tok) return 0;
        if (tok[0] >= '0' && tok[0] <= '9') return (uint8_t)atoi(tok);
        if (!std::strcmp(tok, "simple"))        return 1;  // LEARNING_SIMPLE
        if (!std::strcmp(tok, "normal"))        return 2;  // LEARNING_NORMAL
        if (!std::strcmp(tok, "magic_xor1"))    return 4;
        if (!std::strcmp(tok, "magic_serial1")) return 6;
        if (!std::strcmp(tok, "magic_serial2")) return 7;
        if (!std::strcmp(tok, "magic_serial3")) return 8;
        return 0;  // unknown → decoders will try several modes
    }

private:
    static inline MfKey keys_[MAX_KEYS] = {};
    static inline size_t count_ = 0;
};

}  // namespace pp
