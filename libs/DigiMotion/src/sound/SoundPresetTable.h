/*
 * DigiMotion - SoundPresetTable (Layer 4 sound; Phase A-ε-2 commit 1, D-new-1b)
 * Copyright (C) 2026 DigiCo LLC
 *
 * Licensed under the GNU Affero General Public License version 3 or later.
 * See LICENSE file for full terms.
 *
 * Origin declaration: original design. Not derived from OttoDIY/OttoDIYLib
 * (GPL-3.0) or any other GPL/AGPL upstream. The full T5 12-item anti-
 * derivation discipline applies — see `plans/active/60_robotics-redesign-
 * implementation-plan.md` §4 for the canonical grep pattern list. Highlights
 * for this header:
 *   - Identifier naming uses the `BEEP_<intent>` prefix family. The legacy
 *     `S_<role>` PascalCase / camelCase preset identifier family from the
 *     upstream is NOT used here (T5 §B grep gate). Intent-based naming
 *     matches DigiCode's IoT education target users (Fab Academy / Factory
 *     Scientist / 高校・高専・大学) per memory:target_users.
 *   - Frequency + duration values are designed independently from upstream
 *     bend-sweep constant sets — value+structure both differ. See 60.md
 *     §4 row 3 for the canonical helper-name grep gate.
 *   - File header license is AGPL-3.0 per the lib license discipline; see
 *     60.md §4 row 10 for the header gate.
 *
 * Platform abstraction (59.md §1.0.1): pure C++, no Arduino.h / freertos /
 * ESP32 API. Verified by tools/check-platform-abstraction.sh Layer 3+ gate.
 *
 * Role (60.md §1 Phase A-ε-2 commit 1 + 59.md §1-7.3 verbatim): provides 16
 * named sound presets as fixed-size sequences of (freq, freq2, duration)
 * steps. IBuzzer / PortableBuzzer iterate the sequence and dispatch each
 * step to playTone() or playBendTone(). Tests verify table completeness +
 * iteration logic without touching real HW.
 *
 * Step encoding:
 *   freq  > 0 && freq2 == 0  → simple tone at freq Hz for duration ms
 *   freq  > 0 && freq2 != 0  → bend sweep from freq → freq2 over duration
 *   freq == 0                 → silent rest for duration ms
 *
 * The 16 preset semantics are documented in 59.md §1-7.3 (intent column);
 * iterative refinement (Phase E user smoke fine-tune per D-new-1b confirmed
 * Session 139 close) may revise numerical values without API changes.
 */

#ifndef DIGIMOTION_SOUND_SOUNDPRESETTABLE_H
#define DIGIMOTION_SOUND_SOUNDPRESETTABLE_H

// Intent-based preset identifiers. Order MUST match the table in
// SoundPresetTable.cpp; tests verify this invariant.
enum SoundPresetId {
    BEEP_NONE = 0,           // sentinel: no sound (stepCount = 0)
    BEEP_SHORT_HIGH,
    BEEP_SHORT_LOW,
    BEEP_TWO_HIGH,
    BEEP_TWO_LOW,
    BEEP_RISING_PAIR,
    BEEP_FALLING_PAIR,
    BEEP_RISING_FAST,
    BEEP_FALLING_SLOW,
    BEEP_QUERY_PAIR,
    BEEP_QUERY_RISING,
    BEEP_FANFARE,
    BEEP_OK_SHORT,
    BEEP_ERROR_DESCENDING,
    BEEP_HIGH_SHORT,
    BEEP_STARTUP,
    BEEP_SHUTDOWN,
    SOUND_PRESET_COUNT
};

struct PresetStep {
    int freq;       // Hz; 0 = rest; > 0 = tone (or bend init when freq2 != 0)
    int freq2;      // Hz bend sweep target; 0 = simple tone (not a bend)
    int duration;   // ms
};

// Fixed-size sequence container. MAX_STEPS = 8 chosen to accommodate the
// longest current preset (BEEP_FANFARE / BEEP_ERROR_DESCENDING = 4 steps)
// plus iterative-refinement headroom.
struct PresetData {
    int stepCount;
    PresetStep steps[8];
};

class SoundPresetTable {
public:
    // Returns a reference to the table entry for `id`. Out-of-range id
    // returns BEEP_NONE (stepCount = 0), so callers iterating a returned
    // sequence are safe without an explicit bounds check at the call site.
    static const PresetData& get(SoundPresetId id);
};

#endif  // DIGIMOTION_SOUND_SOUNDPRESETTABLE_H
