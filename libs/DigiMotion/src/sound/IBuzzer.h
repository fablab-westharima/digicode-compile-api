/*
 * DigiMotion - IBuzzer + PortableBuzzer (Layer 4 sound; Phase A-ε-2 commit 1)
 * Copyright (C) 2026 DigiCo LLC
 *
 * Licensed under the GNU Affero General Public License version 3 or later.
 * See LICENSE file for full terms.
 *
 * Origin declaration: original design. Not derived from OttoDIY/OttoDIYLib
 * (GPL-3.0) or any other GPL/AGPL upstream. See 60.md §4 for the canonical
 * T5 anti-derivation grep pattern list — the new design avoids the
 * upstream's preset identifier family, switch-case mapping shape, and
 * specific bend-tone signature.
 *
 * Platform abstraction (59.md §1.0.1): this header is pure C++; the in-
 * memory PortableBuzzer base has no Arduino.h / freertos / ESP32 API
 * dependency. The ESP32 backend lives in DigiBuzzer_esp32.cpp, body-
 * guarded by ARDUINO_ARCH_ESP32 so the native (host) unit-test env
 * compiles it to an empty translation unit.
 *
 * Role (60.md §1 Phase A-ε-2 commit 1 verbatim): blocking buzzer API used
 * by Layer 5 robot libs (via DigiBiped::attachBuzzer + playGesture) and
 * directly by Blockly generators (Phase B buzzer_play_tone / preset /
 * bend_tone / stop blocks). Designed for ad-hoc audio feedback on any
 * ESP32 board, independent of the robot lib family.
 *
 * Iteration model (Session 139 user-confirmed D-new-1b "iterative
 * refinement"): the 16 SoundPresetTable entries are candidate values for
 * Phase E user-smoke evaluation; numerical fine-tuning is expected.
 */

#ifndef DIGIMOTION_SOUND_IBUZZER_H
#define DIGIMOTION_SOUND_IBUZZER_H

#include "SoundPresetTable.h"

class IBuzzer {
public:
    virtual ~IBuzzer() = default;

    // === lifecycle ===
    // Implementations record the pin and (on ESP32) configure pinMode.
    // pin < 0 is treated as "no pin assigned"; subsequent play* calls
    // become no-ops until a valid pin is attached.
    virtual void attach(int pin) = 0;
    virtual void detach() = 0;
    virtual bool isAttached() const = 0;

    // === audio ===
    // Blocking by design: playTone holds the tone for durationMs then
    // releases. Native host backend records the call without delaying.
    // Negative / zero arguments are silent no-ops.
    virtual void playTone(int freqHz, int durationMs) = 0;

    // Sweep from initFreq to endFreq over totalDurationMs. Implementations
    // discretize into a small number of intermediate tones (ESP32 backend
    // uses 8 steps; tests may inspect the discretization or treat it as
    // an opaque bend).
    virtual void playBendTone(int initFreq, int endFreq, int totalDurationMs) = 0;

    // Iterate the SoundPresetTable entry for `presetId` and dispatch each
    // step to playTone (or playBendTone for steps with freq2 != 0).
    // BEEP_NONE / out-of-range id is a no-op. Default impl in
    // PortableBuzzer below — subclasses normally do not override.
    virtual void playPreset(SoundPresetId presetId) = 0;

    // Immediate silence. Safe to call regardless of attach state.
    virtual void stop() = 0;
};

// In-memory base. DigiBuzzer_esp32 inherits + overrides playTone /
// playBendTone / stop with Arduino tone() calls. Tests inherit + override
// the same three methods to record calls without HW dependency. attach /
// detach / isAttached / playPreset are common across all impls and live
// here.
class PortableBuzzer : public IBuzzer {
public:
    PortableBuzzer() : _pin(-1), _attached(false) {}

    void attach(int pin) override {
        _pin = pin;
        _attached = (pin >= 0);
    }
    void detach() override {
        _attached = false;
        _pin = -1;
    }
    bool isAttached() const override { return _attached; }

    // Dispatch loop: each step is either a tone (freq > 0, freq2 == 0),
    // a bend (freq > 0, freq2 != 0), or a rest (freq == 0). Rests are
    // backend-specific — the ESP32 backend delays for the duration; the
    // host (test) backend may treat rests as recorded calls or ignore
    // them. The base impl emits a tone/bend dispatch and leaves rest
    // handling to the subclass via the _rest hook.
    void playPreset(SoundPresetId presetId) override {
        if (!_attached) return;
        const PresetData& p = SoundPresetTable::get(presetId);
        for (int i = 0; i < p.stepCount; ++i) {
            const PresetStep& s = p.steps[i];
            if (s.freq > 0 && s.freq2 != 0) {
                playBendTone(s.freq, s.freq2, s.duration);
            } else if (s.freq > 0) {
                playTone(s.freq, s.duration);
            } else {
                _rest(s.duration);
            }
        }
    }

    int pin() const { return _pin; }

protected:
    // Backend-specific rest hook (silent delay on ESP32; no-op on host
    // tests by default). Subclasses override only if rest accounting
    // matters.
    virtual void _rest(int /*durationMs*/) {}

    int _pin;
    bool _attached;
};

// Singleton accessor implemented per platform. The host (native) test
// env does not link the ESP32 impl, so tests construct a local subclass
// of PortableBuzzer instead of calling this accessor. Layer 5 robot libs
// / generator emit call this on ESP32 to share one buzzer instance.
IBuzzer& getBuzzer();

#endif  // DIGIMOTION_SOUND_IBUZZER_H
