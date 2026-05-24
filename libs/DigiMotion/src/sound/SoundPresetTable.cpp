/*
 * DigiMotion - SoundPresetTable data (Phase A-ε-2 commit 1, D-new-1b)
 * Copyright (C) 2026 DigiCo LLC
 * Licensed under AGPL-3.0-or-later. See LICENSE.
 *
 * 16 preset entries — design intent column in 59.md §1-7.3. Numerical
 * values are independent of the upstream constants; iterative refinement
 * (Phase E user smoke) may revise.
 *
 * Anti-derivation discipline: 60.md §4 lists the canonical T5 grep
 * patterns; this file enumerates only the new design and contains no
 * upstream identifier literals.
 */

#include "SoundPresetTable.h"

namespace {

// Order MUST match the SoundPresetId enum in SoundPresetTable.h.
const PresetData kPresets[SOUND_PRESET_COUNT] = {
    /* BEEP_NONE             */ {0, {{0, 0, 0}}},
    /* BEEP_SHORT_HIGH       */ {1, {{1000, 0,  80}}},
    /* BEEP_SHORT_LOW        */ {1, {{ 400, 0,  80}}},
    /* BEEP_TWO_HIGH         */ {2, {{1000, 0,  80}, {1200, 0,  80}}},
    /* BEEP_TWO_LOW          */ {2, {{ 400, 0,  80}, { 350, 0,  80}}},
    /* BEEP_RISING_PAIR      */ {2, {{ 600, 0, 100}, { 900, 0, 100}}},
    /* BEEP_FALLING_PAIR     */ {2, {{ 900, 0, 100}, { 600, 0, 100}}},
    /* BEEP_RISING_FAST      */ {1, {{ 400, 1200, 200}}},
    /* BEEP_FALLING_SLOW     */ {1, {{1200,  400, 800}}},
    /* BEEP_QUERY_PAIR       */ {2, {{ 800, 0,  80}, {1000, 0, 120}}},
    /* BEEP_QUERY_RISING     */ {3, {{ 600, 0,  60}, { 800, 0,  60}, {1000, 0, 100}}},
    /* BEEP_FANFARE          */ {4, {{ 523, 0, 100}, { 659, 0, 100}, { 784, 0, 100}, {1047, 0, 150}}},
    /* BEEP_OK_SHORT         */ {2, {{ 800, 0,  60}, {1200, 0, 100}}},
    /* BEEP_ERROR_DESCENDING */ {4, {{1500, 0, 100}, {1200, 0, 100}, { 900, 0, 100}, { 600, 0, 150}}},
    /* BEEP_HIGH_SHORT       */ {1, {{1500, 0,  50}}},
    /* BEEP_STARTUP          */ {3, {{ 523, 0,  80}, { 784, 0,  80}, {1047, 0, 120}}},
    /* BEEP_SHUTDOWN         */ {3, {{1047, 0,  80}, { 784, 0,  80}, { 523, 0, 120}}},
};

}  // namespace

const PresetData& SoundPresetTable::get(SoundPresetId id) {
    if (id < 0 || id >= SOUND_PRESET_COUNT) return kPresets[BEEP_NONE];
    return kPresets[id];
}
