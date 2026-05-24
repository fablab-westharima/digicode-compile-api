/*
 * DigiMotion - GestureLibrary (Layer 4 sound; Phase A-ε-2 commit 2, D-new-1a)
 * Copyright (C) 2026 DigiCo LLC
 *
 * Licensed under the GNU Affero General Public License version 3 or later.
 * See LICENSE file for full terms.
 *
 * Origin declaration: original design. Not derived from OttoDIY/OttoDIYLib
 * (GPL-3.0) or any other GPL/AGPL upstream. The full T5 12-item anti-
 * derivation discipline applies — see `plans/active/60_robotics-redesign-
 * implementation-plan.md` §4 for the canonical grep pattern list.
 * Highlights for this header:
 *   - Identifier naming uses the `GESTURE_<intent>` prefix family. The
 *     upstream's emotion-based gesture identifier set (see 60.md §4 row 1
 *     grep gate) is NOT reproduced. Intent-based naming matches DigiCode's
 *     IoT-education target users (Fab Academy / Factory Scientist / 高校・
 *     高専・大学) per memory:target_users — the audience needs to see what
 *     the robot is *doing* (acknowledging / searching / confirming), not
 *     anthropomorphized emotion labels.
 *   - playGesture dispatch (in DigiBiped) uses a runtime GestureDefinition
 *     lookup, not the upstream switch-case mapping shape (60.md §4 row 9).
 *   - Per-gesture motion pattern (amp[4] + phase[4] + cycles + periodMs)
 *     is fed to N independent SineOscillator instances (Layer 4) → bank
 *     batch-publish (Layer 3) → channel pump (Layer 2). The upstream
 *     multi-servo helper-signature pair (60.md §4 rows 7-8 grep gates) is
 *     structurally unused — this design has no equivalent.
 *
 * Platform abstraction (59.md §1.0.1): pure C++ + SoundPresetTable. No
 * Arduino.h / freertos / ESP32 API. Verified by tools/check-platform-
 * abstraction.sh Layer 3+ gate.
 *
 * Role (60.md §1 Phase A-ε-2 commit 2 + 59.md §1-7.2 verbatim): 14
 * candidate gestures as (motion + sound) compositions. DigiBiped consumes
 * via attachBuzzer(IBuzzer*) + playGesture(GestureId, nowMs). Iterative
 * refinement (Session 139 user-confirmed D-new-1a, Phase E user smoke) may
 * revise numerical values without API change.
 *
 * Motion-shape independence from existing DigiBiped MotionShape entries
 * (WALK / TURN / JUMP / DANCE / SWING / BEND / MOONWALK): every gesture
 * amp/phase combination is numerically distinct from those 7 shapes AND
 * from the upstream `{30,30,20,20}` + `{0,0,90,90}` walk constants. The
 * gesture set deliberately covers a wider design space (negative amps for
 * lean, single-channel selective amps, irregular cycle counts).
 */

#ifndef DIGIMOTION_SOUND_GESTURELIBRARY_H
#define DIGIMOTION_SOUND_GESTURELIBRARY_H

#include "SoundPresetTable.h"

// Intent-based gesture identifiers. Order MUST match the table in
// GestureLibrary.cpp; tests verify this invariant.
enum GestureId {
    GESTURE_NONE = 0,           // sentinel: no motion + no sound
    GESTURE_GREETING,
    GESTURE_ACKNOWLEDGE,
    GESTURE_YES,
    GESTURE_NO,
    GESTURE_CURIOSITY,
    GESTURE_SEARCH,
    GESTURE_IDLE_BREATHING,
    GESTURE_CHEER,
    GESTURE_THINKING,
    GESTURE_SURPRISE,
    GESTURE_SLEEPY,
    GESTURE_WAKEUP,
    GESTURE_CONFIRMATION,
    GESTURE_ERROR_ALERT,
    GESTURE_COUNT
};

// Per-gesture sine-oscillator motion parameters. Channels are indexed per
// DigiBiped::ChannelIndex (LEFT_LEG=0 / RIGHT_LEG=1 / LEFT_FOOT=2 /
// RIGHT_FOOT=3) — case 23 incident D label reconciliation single source.
//
// amp[i]:    amplitude in degrees (signed; sign reverses the sine phase)
// phase[i]:  initial phase in radians
// cycles:    number of full sine cycles before returning to home + IDLE
// periodMs:  period of one sine cycle
//
// cycles == 0 or periodMs == 0 → motion is skipped (e.g. silent / sound-
// only gestures, currently none but the encoding is reserved).
struct GesturePattern {
    int amp[4];
    double phase[4];
    int cycles;
    int periodMs;
};

struct GestureDefinition {
    GesturePattern motion;
    SoundPresetId sound;   // BEEP_NONE = no sound on this gesture
};

class GestureLibrary {
public:
    // Returns a reference to the table entry for `id`. Out-of-range id
    // returns GESTURE_NONE (cycles = 0 + sound = BEEP_NONE), so callers
    // are safe without an explicit bounds check.
    static const GestureDefinition& get(GestureId id);
};

#endif  // DIGIMOTION_SOUND_GESTURELIBRARY_H
