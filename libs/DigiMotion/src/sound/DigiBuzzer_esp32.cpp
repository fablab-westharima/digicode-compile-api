/*
 * DigiMotion - DigiBuzzer (ESP32 platform impl, Phase A-ε-2 commit 1)
 * Copyright (C) 2026 DigiCo LLC
 *
 * Licensed under the GNU Affero General Public License version 3 or later.
 * See LICENSE file for full terms.
 *
 * Origin declaration: original ESP32 implementation. Uses the Arduino-
 * ESP32 core's tone() / noTone() / delay() — these are framework-level
 * functions, not upstream lib helpers. See 60.md §4 for the canonical
 * anti-derivation grep gate list; no upstream sound-helper signature is
 * reproduced.
 *
 * Platform abstraction (59.md §1.0.1): the entire body is guarded by
 * ARDUINO_ARCH_ESP32 so the native (host) unit-test env compiles it to
 * an empty translation unit. <Arduino.h> usage is confined to this file.
 * tone() / noTone() / delay() are not listed in check-platform-
 * abstraction.sh's Layer 3+ forbidden set because they are Arduino-core
 * framework calls (not ESP32-only hardware APIs); an RP2040 sibling
 * _rp2040.cpp would call the same Arduino-Pico equivalents.
 *
 * Implementation choices:
 *   - playTone:     tone(pin, freq, duration) + delay(duration) +
 *                   noTone(pin). Blocking by design (60.md §1-7.4
 *                   verbatim sketch). Duration delay ensures the tone
 *                   plays out before the next playTone in a sequence.
 *   - playBendTone: discretize the sweep into N=8 steps. Linear
 *                   frequency interpolation; 8 was chosen as a tradeoff
 *                   between audible smoothness and per-step overhead.
 *                   Iterative refinement (Phase E) may revise.
 *   - playPreset:   inherited from PortableBuzzer; iterates the table
 *                   and dispatches each step. The _rest hook is
 *                   overridden here to call delay() so silent rests in
 *                   future preset designs are correctly timed.
 */

#ifdef ARDUINO_ARCH_ESP32

#include <Arduino.h>

#include "IBuzzer.h"

namespace {

class DigiBuzzer : public PortableBuzzer {
public:
    void attach(int pin) override {
        PortableBuzzer::attach(pin);
        if (_attached) pinMode(_pin, OUTPUT);
    }

    void playTone(int freqHz, int durationMs) override {
        if (!_attached || freqHz <= 0 || durationMs <= 0) return;
        tone(_pin, freqHz, durationMs);
        delay(durationMs);
        noTone(_pin);
    }

    void playBendTone(int initFreq, int endFreq, int totalDurationMs) override {
        if (!_attached || totalDurationMs <= 0) return;
        constexpr int STEPS = 8;
        int stepMs = totalDurationMs / STEPS;
        if (stepMs < 1) stepMs = 1;
        for (int i = 0; i < STEPS; ++i) {
            // Linear interpolation freq[i] = init + (end - init) * i / (STEPS - 1).
            // Using integer math; small rounding error is inaudible.
            int freq = initFreq + (endFreq - initFreq) * i / (STEPS - 1);
            if (freq > 0) {
                tone(_pin, freq, stepMs);
                delay(stepMs);
            }
        }
        noTone(_pin);
    }

    void stop() override {
        if (_attached) noTone(_pin);
    }

protected:
    void _rest(int durationMs) override {
        if (durationMs > 0) delay(durationMs);
    }
};

DigiBuzzer _esp32BuzzerInstance;

}  // namespace

IBuzzer& getBuzzer() {
    return _esp32BuzzerInstance;
}

#endif  // ARDUINO_ARCH_ESP32
