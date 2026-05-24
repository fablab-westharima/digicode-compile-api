/*
 * DigiMotion - ChannelBank (Layer 3)
 * Copyright (C) 2026 DigiCo LLC
 *
 * Licensed under the GNU Affero General Public License version 3 or later.
 * See LICENSE file for full terms.
 *
 * Origin declaration: original design. Not derived from OttoDIY/OttoDIYLib
 * (GPL-3.0) or any other GPL/AGPL upstream. The grouping + barrier concept
 * is generic coordination logic; the API surface (addChannel / setTargets /
 * allReached) is new source written for this layer. T5 §4 + §11 anti-
 * derivation grep gates verify no Oscillator PascalCase prefixes or
 * humanoid/transform/wheel instance names appear in this file.
 *
 * Platform abstraction (59.md §1.0.1): pure C++ pointer + array logic. No
 * Arduino.h / freertos / ESP32 API. Depends only on IActuatorChannel.h
 * (Layer 2 IF). Verified by tools/check-platform-abstraction.sh Layer 3
 * gate.
 *
 * Role (60.md §1 Phase A-δ verbatim): N-channel grouping for Layer 5
 * robot libs (DigiBiped etc.). Provides atomic setTargets() and a non-
 * blocking allReached() predicate. Each held channel is pumped
 * independently by Layer 1 BackgroundPump (registered when the channel
 * is attached); the bank itself does NOT inherit IPumpable — it is pure
 * coordination state.
 */

#ifndef DIGIMOTION_MOTION_CHANNELBANK_H
#define DIGIMOTION_MOTION_CHANNELBANK_H

#include "../actuator/IActuatorChannel.h"

class ChannelBank {
public:
    // constexpr (C++17 implicit inline) so test code may take its address
    // without an out-of-class definition (Session 141 §5.1 教訓).
    // 16 = upper bound across the Phase A-ε robot libs: DigiBiped 4 servos
    // + future extension headroom (DigiQuad 12, DigiSpider 16).
    static constexpr int MAX_CHANNELS = 16;

    ChannelBank();

    // Add channel to the bank. nullptr / duplicate / full bank return false.
    // The bank does NOT take ownership of the channel; lifetime is the
    // caller's responsibility.
    bool addChannel(IActuatorChannel* channel);

    // Remove channel from the bank. Returns true if found and removed.
    bool removeChannel(IActuatorChannel* channel);

    int count() const;
    IActuatorChannel* at(int index) const;  // nullptr if out of range

    // Atomic batch: set target[i] on channel i for i in [0, n).
    // n must equal count() for the batch to apply uniformly; mismatches
    // are clamped to min(n, count()). For phase-sync motion (4 servos
    // driven by 4 SineOscillators sharing one startMs), Layer 5 calls
    // this every tick with the per-oscillator valueAt(nowMs) values.
    void setTargets(const long* targets, int n);

    // Set the target on a single channel by bank index. Out-of-range is
    // silently ignored.
    void setTarget(int index, long target);

    // Non-blocking barrier predicate: true iff every channel's
    // hasReachedTarget() is true. Layer 5 calls this from its own
    // IPumpable to wait for blocking motion completion. Layer 3+ never
    // delays inside (case 23 incident A discipline).
    bool allReached() const;

private:
    IActuatorChannel* _channels[MAX_CHANNELS];
    int _count;
};

#endif  // DIGIMOTION_MOTION_CHANNELBANK_H
