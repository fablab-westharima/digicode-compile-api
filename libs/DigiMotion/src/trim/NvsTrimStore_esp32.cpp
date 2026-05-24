/*
 * DigiMotion - NVS-backed TrimStore (ESP32 platform impl, Phase A-ε commit 1)
 * Copyright (C) 2026 DigiCo LLC
 *
 * Licensed under the GNU Affero General Public License version 3 or later.
 * See LICENSE file for full terms.
 *
 * Origin declaration: original ESP32 implementation. D6 = EEPROM 完全
 * 廃止 (case 23 incident C structural defense); this is the NVS
 * replacement built on the ESP32 Arduino Preferences API. No code is
 * git-mv'd from the legacy humanoid lib's loadTrimsFromEEPROM /
 * saveTrimsToEEPROM helpers; identifier set and storage schema are
 * intentionally distinct.
 *
 * Platform abstraction (59.md §1.0.1): the entire body is guarded by
 * ARDUINO_ARCH_ESP32 so the native (host) unit-test env compiles it to
 * an empty translation unit. Preferences.h / NVS API usage is confined
 * to this file. The check-platform-abstraction.sh script's Layer 3+
 * gate does not list <Preferences.h> as forbidden because (a) it is the
 * documented platform-specific persistence backend for this single
 * subsystem and (b) on RP2040 future work a sibling _rp2040.cpp would
 * use a different backend (LittleFS or similar) and #ifdef gate.
 *
 * Storage schema (E3 = "servo_trim_v2" namespace):
 *   namespace = "servo_trim_v2"  (旧 "servo_trim" は schema 互換性なく完全廃止)
 *   key       = "version" -> int (currently 1; reserved for future migrations)
 *   key       = "t<pin>"  -> int (trim deg for that pin, clamped to ±30)
 *
 * Out-of-range values read from NVS (e.g. corrupted entry, post-flash
 * stale data) are re-clamped on load to the [TRIM_MIN, TRIM_MAX] window.
 *
 * The old OTA-template "servo_trim" namespace with "count" + "s0..s15"
 * keys (case 23 incident B = orphan storage) is abandoned wholesale per
 * D6. Existing firmware that has the old keys leaves them in NVS
 * harmlessly; the new namespace is independent and does not read them.
 */

#ifdef ARDUINO_ARCH_ESP32

#include <Arduino.h>
#include <Preferences.h>
#include <stdio.h>

#include "ITrimStore.h"

namespace {

constexpr const char* NVS_NAMESPACE  = "servo_trim_v2";
constexpr const char* KEY_VERSION    = "version";
constexpr int          SCHEMA_VERSION = 1;

class NvsTrimStore : public PortableTrimStore {
public:
    void load() override {
        Preferences prefs;
        if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/true)) return;
        // version key is informational at schema 1; future migrations
        // (e.g. schema 2 with per-pin pulse range) would branch on it.
        (void)prefs.getInt(KEY_VERSION, 0);
        for (int pin = 0; pin < MAX_PIN; ++pin) {
            char key[8];
            snprintf(key, sizeof(key), "t%d", pin);
            int v = prefs.getInt(key, 0);
            if (v < TRIM_MIN) v = TRIM_MIN;
            if (v > TRIM_MAX) v = TRIM_MAX;
            _trims[pin] = v;
        }
        prefs.end();
    }

    void save() override {
        Preferences prefs;
        if (!prefs.begin(NVS_NAMESPACE, /*readOnly=*/false)) return;
        prefs.putInt(KEY_VERSION, SCHEMA_VERSION);
        for (int pin = 0; pin < MAX_PIN; ++pin) {
            char key[8];
            snprintf(key, sizeof(key), "t%d", pin);
            prefs.putInt(key, _trims[pin]);
        }
        prefs.end();
    }
};

NvsTrimStore _esp32TrimStoreInstance;

}  // namespace

ITrimStore& getTrimStore() {
    return _esp32TrimStoreInstance;
}

#endif  // ARDUINO_ARCH_ESP32
