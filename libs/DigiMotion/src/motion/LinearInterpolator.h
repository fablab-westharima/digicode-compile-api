/*
 * DigiMotion - LinearInterpolator (Layer 4)
 * Copyright (C) 2026 DigiCo LLC
 *
 * Licensed under the GNU Affero General Public License version 3 or later.
 * See LICENSE file for full terms.
 *
 * Origin declaration: original design. Not derived from OttoDIY/OttoDIYLib
 * (GPL-3.0) or any other GPL/AGPL upstream. Generic lerp logic, new source.
 *
 * Platform abstraction (59.md §1.0.1): pure C++ integer + clamping math.
 * No Arduino.h / freertos / ESP32 API. Verified by Layer 3+ gate in
 * tools/check-platform-abstraction.sh.
 *
 * Role (60.md §1 Phase A-δ verbatim): "current → target lerp over time、
 * rate-cap aware". Session 142 design judgment (案 B = Layer 2 setMaxRate
 * 委譲): the rate cap is Layer 2 IActuatorChannel::setMaxRate()'s
 * responsibility. LinearInterpolator publishes the unconstrained target
 * trajectory; the channel's pump() enforces the rate cap when stepping
 * _current toward the published target. This decoupling avoids the
 * dual-control trap (case 23 incident A delaying signature: when two
 * layers each clip rate, the user-visible motion no longer matches
 * either setting). "Rate-cap aware" in this layer means: the durationMs
 * a caller passes here is the design-level desired motion time, and
 * Layer 2 setMaxRate is set independently to protect the actuator
 * (founding use case = humanoid gear protection).
 */

#ifndef DIGIMOTION_MOTION_LINEARINTERPOLATOR_H
#define DIGIMOTION_MOTION_LINEARINTERPOLATOR_H

class LinearInterpolator {
public:
    LinearInterpolator();

    // Begin a lerp from startValue to endValue over durationMs starting
    // at nowMs. Calling start() again replaces the prior trajectory.
    void start(unsigned long nowMs, long startValue, long endValue,
               unsigned long durationMs);

    void stop();
    bool isStarted() const;

    long getStartValue() const;
    long getEndValue() const;
    unsigned long getDuration() const;

    // Compute the value at nowMs:
    //   !started OR durationMs == 0  →  returns endValue
    //   nowMs ≤ startMs              →  returns startValue
    //   nowMs ≥ startMs + durationMs →  returns endValue (clamped)
    //   else                         →  startValue + (endValue - startValue)
    //                                   * (nowMs - startMs) / durationMs
    long valueAt(unsigned long nowMs) const;

    // True iff the trajectory has fully elapsed (or was never started, or
    // had zero duration). Layer 5 polls this between ticks for completion.
    bool isDone(unsigned long nowMs) const;

private:
    unsigned long _startMs;
    unsigned long _durationMs;
    long _startValue;
    long _endValue;
    bool _started;
};

#endif  // DIGIMOTION_MOTION_LINEARINTERPOLATOR_H
