/*
 * DigiMotion - IBackgroundPump abstract + IPumpable contract (Layer 1)
 * Copyright (C) 2026 DigiCo LLC
 *
 * Licensed under the GNU Affero General Public License version 3 or later.
 * See LICENSE file for full terms.
 *
 * Origin declaration: original design. Not derived from OttoDIY/OttoDIYLib
 * (GPL-3.0) or any other GPL/AGPL upstream. The 40-entry registry concept
 * generalises the inline _ServoState[40] / _servoBackgroundTask pattern
 * previously embedded in the monorepo's servoBlocks.ts (Session 138), but
 * this header and the accompanying ESP32 impl are new source written for
 * this layer.
 *
 * Platform abstraction (59.md §1.0.1): this header deliberately avoids
 * Arduino.h and freertos/ includes. Layer 2+ may depend on it without
 * dragging ESP32-specific headers into the build. The ESP32-specific
 * task is fully contained in FreeRtosBackgroundPump_esp32.cpp.
 */

#ifndef DIGIMOTION_PUMP_IBACKGROUNDPUMP_H
#define DIGIMOTION_PUMP_IBACKGROUNDPUMP_H

// Anything the BackgroundPump drives must satisfy this contract.
// Layer 2 IActuatorChannel (Phase A-γ commit 2) inherits from IPumpable.
class IPumpable {
public:
    virtual ~IPumpable() = default;

    // Invoked at ~1 ms tick by the pump. Must be non-blocking. Implementations
    // perform their own rate-limit check (e.g. servo: only step one degree
    // every (1000/degPerSec) ms inside pump()).
    virtual void pump(unsigned long nowMs) = 0;

    // Hint for the scheduler. Inactive entries are skipped quickly so an
    // idle pumpable costs only a pointer + bool read per tick.
    virtual bool isActive() const = 0;
};

// Abstract background pump. start()/stop() drive the platform task,
// register/unregister manage the IPumpable slots, tick() runs one scan.
// tick() is exposed so host-side tests can drive scheduling deterministically
// (PortableBackgroundPump below). The ESP32 subclass invokes tick() from
// inside its FreeRTOS task; user code never calls tick() on ESP32.
class IBackgroundPump {
public:
    // constexpr (C++17 implicit inline) so test code may take its address
    // (e.g. `&pumpables[MAX_ENTRIES]`) without an out-of-class definition.
    static constexpr int MAX_ENTRIES = 40;

    virtual ~IBackgroundPump() = default;

    virtual bool start() = 0;
    virtual void stop() = 0;

    // nullptr is rejected. Duplicate pointer is rejected. Returns false when
    // the registry is full.
    virtual bool registerPumpable(IPumpable* p) = 0;

    // Returns true if `p` was found and removed.
    virtual bool unregisterPumpable(IPumpable* p) = 0;

    // One scan: for each registered, active entry, call pump(nowMs).
    virtual void tick(unsigned long nowMs) = 0;

    virtual int registeredCount() const = 0;
};

// Concrete base with the platform-agnostic registry + scan logic. Methods are
// inline so the class can be instantiated in any translation unit (host tests
// or ESP32 firmware) without requiring a separate .cpp file. The ESP32
// subclass (FreeRtosBackgroundPump_esp32.cpp) inherits and overrides
// start()/stop() only.
class PortableBackgroundPump : public IBackgroundPump {
public:
    PortableBackgroundPump() : _count(0) {
        for (int i = 0; i < MAX_ENTRIES; ++i) _entries[i] = nullptr;
    }

    bool start() override { return true; }
    void stop() override {}

    bool registerPumpable(IPumpable* p) override {
        if (p == nullptr) return false;
        for (int i = 0; i < MAX_ENTRIES; ++i) {
            if (_entries[i] == p) return false;
        }
        for (int i = 0; i < MAX_ENTRIES; ++i) {
            if (_entries[i] == nullptr) {
                _entries[i] = p;
                ++_count;
                return true;
            }
        }
        return false;
    }

    bool unregisterPumpable(IPumpable* p) override {
        if (p == nullptr) return false;
        for (int i = 0; i < MAX_ENTRIES; ++i) {
            if (_entries[i] == p) {
                _entries[i] = nullptr;
                --_count;
                return true;
            }
        }
        return false;
    }

    void tick(unsigned long nowMs) override {
        for (int i = 0; i < MAX_ENTRIES; ++i) {
            IPumpable* p = _entries[i];
            if (p == nullptr) continue;
            if (!p->isActive()) continue;
            p->pump(nowMs);
        }
    }

    int registeredCount() const override { return _count; }

protected:
    IPumpable* _entries[MAX_ENTRIES];
    int _count;
};

// Singleton accessor implemented per platform. Layer 2 channels call
// getBackgroundPump().registerPumpable(this) inside attach(). The host
// test env does not link the ESP32 impl, so tests must construct a local
// PortableBackgroundPump instead of calling this accessor.
IBackgroundPump& getBackgroundPump();

#endif // DIGIMOTION_PUMP_IBACKGROUNDPUMP_H
