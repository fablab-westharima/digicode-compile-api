/*
 * DigiMotion - ITrimStore + PortableTrimStore (Layer 4 persist; Phase A-ε commit 1)
 * Copyright (C) 2026 DigiCo LLC
 *
 * Licensed under the GNU Affero General Public License version 3 or later.
 * See LICENSE file for full terms.
 *
 * Origin declaration: original design. Not derived from OttoDIY/OttoDIYLib
 * (GPL-3.0) or any other GPL/AGPL upstream. The EEPROM-backed trim system
 * in the legacy humanoid library (case 23 incident C) is completely
 * abandoned per D6 = EEPROM 完全廃止 + NVS 一本化. All identifiers
 * (ITrimStore, PortableTrimStore, applyToChannel, namespace
 * "servo_trim_v2") are new source.
 *
 * Platform abstraction (59.md §1.0.1): this header is pure C++. The
 * abstract IF and the in-memory PortableTrimStore base have no
 * Arduino.h, Preferences.h, EEPROM.h, or freertos/ dependency. The ESP32
 * NVS backend lives in NvsTrimStore_esp32.cpp, body-guarded by
 * ARDUINO_ARCH_ESP32 so the native (host) unit-test env compiles it to
 * an empty translation unit.
 *
 * Role (60.md §1 Phase A-ε commit 1 verbatim): trim 永続化, NVS namespace
 * "servo_trim_v2", key schema "t<pin>". load/save/get/set/applyToChannel
 * provide the bridge between (a) the ServoTrimDialog UI (Phase D
 * transport) and (b) Layer 2 IActuatorChannel::setTrim(). case 23
 * incident B structural defense — every trim write has a verified
 * consumer path.
 *
 * Lifecycle (Session 143 user-confirmed 案 c, both init-time + event-
 * driven, last-write-wins precedence):
 *   boot:    load(); for each (pin, channel) pair: applyToChannel(...)
 *   runtime: set(pin, value); applyToChannel(channel, pin) immediately
 *            pushes the new value through to HW. The latest call wins;
 *            the store holds no implicit precedence rule — precedence is
 *            the responsibility of the call sequence (Layer 5 robot init
 *            vs ServoTrimDialog command, Phase D).
 *
 * Trim unit is channel-agnostic at this layer. The unit interpretation
 * lives in IActuatorChannel concrete subclasses:
 *   ServoChannel180 / 270  : deg     (-30..+30)
 *   ContinuousServoChannel : deg     (offset on the 90° stop center)
 *   StepperPollChannel     : steps
 *   StepperHwChannel       : steps
 *   DcMotorChannel         : %       (deadband compensation)
 * The store clamps to [TRIM_MIN, TRIM_MAX] = [-30, +30]. Channels whose
 * native trim range is narrower or differently scaled clamp again
 * inside their own setTrim() (e.g. ServoChannel180 already clamps to
 * ±30°, matching this range).
 */

#ifndef DIGIMOTION_TRIM_ITRIMSTORE_H
#define DIGIMOTION_TRIM_ITRIMSTORE_H

#include "../actuator/IActuatorChannel.h"

class ITrimStore {
public:
    virtual ~ITrimStore() = default;

    // === persistence hooks ===
    // Implementations may rely on a platform-specific backend (NVS,
    // flash, file system). The portable base treats these as no-ops;
    // tests inject a mock backend by subclassing.
    virtual void load() = 0;
    virtual void save() = 0;

    // === in-memory state ===
    // pin: physical GPIO number. trimDeg is the unit-agnostic per-
    // channel offset; the channel interprets it per its own setTrim()
    // semantics. Out-of-range pin (<0 or >=MAX_PIN) is silently ignored
    // / returns 0.
    virtual void set(int pin, int trimDeg) = 0;
    virtual int  get(int pin) const = 0;
    virtual void clearAll() = 0;
    virtual int  count() const = 0;  // pins with non-zero trim

    // === Layer 2 push ===
    // Applies the stored trim for `pin` to `channel` via
    // channel->setTrim(). No-op if channel == nullptr or pin out of
    // range. case 23 incident A defense: this is the documented bridge
    // from storage to HW reflection.
    virtual void applyToChannel(IActuatorChannel* channel, int pin) = 0;
};

// In-memory base. NvsTrimStore_esp32 inherits + overrides load()/save()
// with Preferences calls. Tests inherit + override with a mock backend
// (see test_trim_store).
//
// The in-memory state is a fixed array indexed by pin number, bounded
// by MAX_PIN = 40 (ESP32 GPIO upper bound). Wastes ~160 bytes vs a
// sparse representation but keeps lookup O(1) and the IF surface
// minimal.
class PortableTrimStore : public ITrimStore {
public:
    static constexpr int MAX_PIN  = 40;
    static constexpr int TRIM_MIN = -30;
    static constexpr int TRIM_MAX = 30;

    PortableTrimStore() {
        for (int i = 0; i < MAX_PIN; ++i) _trims[i] = 0;
    }

    void load() override {}  // no-op base; subclass reads from backend
    void save() override {}  // no-op base; subclass writes to backend

    void set(int pin, int trimDeg) override {
        if (pin < 0 || pin >= MAX_PIN) return;
        if (trimDeg < TRIM_MIN) trimDeg = TRIM_MIN;
        if (trimDeg > TRIM_MAX) trimDeg = TRIM_MAX;
        _trims[pin] = trimDeg;
    }

    int get(int pin) const override {
        if (pin < 0 || pin >= MAX_PIN) return 0;
        return _trims[pin];
    }

    void clearAll() override {
        for (int i = 0; i < MAX_PIN; ++i) _trims[i] = 0;
    }

    int count() const override {
        int n = 0;
        for (int i = 0; i < MAX_PIN; ++i) {
            if (_trims[i] != 0) ++n;
        }
        return n;
    }

    void applyToChannel(IActuatorChannel* channel, int pin) override {
        if (channel == nullptr) return;
        if (pin < 0 || pin >= MAX_PIN) return;
        channel->setTrim(_trims[pin]);
    }

protected:
    int _trims[MAX_PIN];
};

// Singleton accessor implemented per platform. The host (native) test
// env does not link the ESP32 impl, so tests construct a local
// PortableTrimStore (or a test subclass) instead of calling this
// accessor. Layer 5 robot libs / OTA template runtime call this on
// ESP32 to share one store across all channels.
ITrimStore& getTrimStore();

#endif  // DIGIMOTION_TRIM_ITRIMSTORE_H
