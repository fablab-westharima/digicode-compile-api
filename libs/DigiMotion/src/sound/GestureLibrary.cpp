/*
 * DigiMotion - GestureLibrary data (Phase A-ε-2 commit 2, D-new-1a)
 * Copyright (C) 2026 DigiCo LLC
 * Licensed under AGPL-3.0-or-later. See LICENSE.
 *
 * 14 gesture entries — intent column + motion sketch in 59.md §1-7.2.
 * Numerical amplitudes / phases / cycles / period are independent of
 * upstream constants; iterative refinement (Phase E user smoke per
 * D-new-1a confirmed Session 139 close) may revise.
 *
 * Anti-derivation discipline: 60.md §4 lists the canonical grep gates;
 * this file enumerates only the new design intents and contains no
 * upstream identifier literals.
 *
 * π constants are inlined rather than including <cmath> (host-test
 * compatibility + Arduino-AVR future portability per D-new-2).
 */

#include "GestureLibrary.h"

namespace {

constexpr double PI_      = 3.14159265358979;
constexpr double HALF_PI_ = 1.57079632679490;

// Order MUST match the GestureId enum in GestureLibrary.h.
//
// Motion-pattern design notes (intent → amp/phase rationale):
//   - LEGs = LEFT_LEG / RIGHT_LEG indices 0,1; FEET = LEFT_FOOT /
//     RIGHT_FOOT indices 2,3.
//   - Negative amp = sine flipped (initial motion in opposite direction).
//     Used for "lean forward" effects in GREETING / CURIOSITY / THINKING
//     / SLEEPY where the body angles forward of the home center.
//   - Phase π between LEFT/RIGHT pairs = anti-phase (one limb forward
//     while the other is back). Used for NO / CURIOSITY (side sway) /
//     SEARCH.
//
// Each row deliberately differs from DigiBiped's WALK/TURN/JUMP/DANCE/
// SWING/BEND/MOONWALK static shapes both numerically AND structurally
// (no full 4-quarter-phase rotation; no symmetric {30,30,20,20} shape).
const GestureDefinition kGestures[GESTURE_COUNT] = {
    /* GESTURE_NONE           */ {{{0, 0, 0, 0}, {0.0, 0.0, 0.0, 0.0}, 0, 0}, BEEP_NONE},

    /* GESTURE_GREETING       */ {{{-15, -15, 0, 0}, {0.0, 0.0, 0.0, 0.0}, 1, 2000}, BEEP_RISING_PAIR},
    /* GESTURE_ACKNOWLEDGE    */ {{{0, 0, 12, 12},   {0.0, 0.0, 0.0, 0.0}, 2,  600}, BEEP_SHORT_LOW},
    /* GESTURE_YES            */ {{{15, 15, 15, 15}, {0.0, 0.0, 0.0, 0.0}, 2,  800}, BEEP_TWO_HIGH},
    /* GESTURE_NO             */ {{{20, 20, 0, 0},   {0.0, PI_, 0.0, 0.0}, 2,  800}, BEEP_TWO_LOW},
    /* GESTURE_CURIOSITY      */ {{{-10, -10, 15, 15}, {0.0, 0.0, 0.0, PI_}, 1, 1200}, BEEP_QUERY_PAIR},
    /* GESTURE_SEARCH         */ {{{30, 30, 0, 0},   {0.0, PI_, 0.0, 0.0}, 3,  800}, BEEP_NONE},
    /* GESTURE_IDLE_BREATHING */ {{{3, 3, 3, 3},     {0.0, 0.0, 0.0, 0.0}, 5, 2000}, BEEP_NONE},
    /* GESTURE_CHEER          */ {{{0, 0, 35, 35},   {0.0, 0.0, 0.0, 0.0}, 2,  500}, BEEP_FANFARE},
    /* GESTURE_THINKING       */ {{{-5, 0, 0, 0},    {0.0, 0.0, 0.0, 0.0}, 1, 1500}, BEEP_QUERY_RISING},
    /* GESTURE_SURPRISE       */ {{{20, 20, 0, 0},   {0.0, 0.0, 0.0, 0.0}, 1,  200}, BEEP_HIGH_SHORT},
    /* GESTURE_SLEEPY         */ {{{-15, -15, -15, -15}, {0.0, 0.0, 0.0, 0.0}, 1, 3000}, BEEP_FALLING_SLOW},
    /* GESTURE_WAKEUP         */ {{{15, 15, 0, 0},   {0.0, 0.0, 0.0, 0.0}, 1, 2000}, BEEP_RISING_FAST},
    /* GESTURE_CONFIRMATION   */ {{{0, 0, 15, 15},   {0.0, 0.0, 0.0, 0.0}, 1,  400}, BEEP_OK_SHORT},
    /* GESTURE_ERROR_ALERT    */ {{{15, 15, 15, 15}, {0.0, 0.0, HALF_PI_, HALF_PI_}, 2, 200}, BEEP_ERROR_DESCENDING},
};

}  // namespace

const GestureDefinition& GestureLibrary::get(GestureId id) {
    if (id < 0 || id >= GESTURE_COUNT) return kGestures[GESTURE_NONE];
    return kGestures[id];
}
