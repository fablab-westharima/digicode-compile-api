/*
 * DigiMotion - SineOscillator (Layer 4)
 * Copyright (C) 2026 DigiCo LLC
 *
 * Licensed under the GNU Affero General Public License version 3 or later.
 * See LICENSE file for full terms.
 *
 * Origin declaration: original design. Not derived from OttoDIY/OttoDIYLib
 * (GPL-3.0) Oscillator class. T5 §4 anti-derivation discipline:
 *   - Naming: camelCase full English (setAmplitude / setOffset / setPeriod /
 *     setPhase / start / stop / reset / valueAt). The OttoDIYLib Oscillator
 *     PascalCase prefix family (SetA / SetO / SetPh / SetT / SetTrim /
 *     SetLimiter / DisableLimiter / Stop / Play / Reset) is NOT used.
 *   - Algorithm: direct evaluation valueAt(nowMs) = offset +
 *     amplitude * sin(2π * (nowMs - startMs) / periodMs + phaseRad).
 *     No refresh() polling, no _trim member, no _stopped / _phase0 /
 *     _diff_limit fields, no internal limiter rate clamp. Rate clamping
 *     is the Layer 2 IActuatorChannel::setMaxRate() responsibility per
 *     Session 142 design judgment (案 B): Layer 4 publishes pure target
 *     values and Layer 2 pump() enforces the rate cap.
 *   - Field set: _amplitude / _offset / _periodMs / _phaseRad / _startMs /
 *     _started. Independent of Otto Oscillator's _amplitude / _offset /
 *     _period / _phase0 / _trim / _diff_limit / _ref / _previousMillis /
 *     _previousServoCommandTime / _pos / _stop layout.
 *
 * Platform abstraction (59.md §1.0.1): pure C++ + <cmath> sin(). No
 * Arduino.h / freertos / ESP32 API. Verified by Layer 3+ gate in
 * tools/check-platform-abstraction.sh.
 *
 * Role (60.md §1 Phase A-δ verbatim): "amplitude + offset + period +
 * phase の sin wave 計算、ChannelBank に target publish". Implementation
 * choice: pure value generator. Layer 5 robot libs (Phase A-ε) call
 * valueAt(nowMs) and forward the result through ChannelBank.setTargets().
 * Decoupling from ChannelBank simplifies host testing (no IActuatorChannel
 * mocks needed) and matches the rule-18 §Discipline 1 path "publisher →
 * coordinator → channel" with each link independently verifiable.
 */

#ifndef DIGIMOTION_MOTION_SINEOSCILLATOR_H
#define DIGIMOTION_MOTION_SINEOSCILLATOR_H

class SineOscillator {
public:
    SineOscillator();

    // Configuration. Units are caller-defined (interpreted by the channel
    // type the value is eventually written to): for ServoChannel180,
    // amplitude/offset are degrees, periodMs is ms, phaseRad is radians.
    void setAmplitude(int amplitude);
    void setOffset(int offset);
    void setPeriod(unsigned long periodMs);  // 0 → guard, valueAt returns offset
    void setPhase(double phaseRad);

    int getAmplitude() const;
    int getOffset() const;
    unsigned long getPeriod() const;
    double getPhase() const;

    // Records startMs and marks the oscillator as started. valueAt(nowMs)
    // computes against this anchor until stop() or another start().
    void start(unsigned long nowMs);
    void stop();
    bool isStarted() const;

    // Compute target value at nowMs:
    //   if !started OR periodMs == 0  → returns offset (rest position)
    //   else                          → offset + amplitude * sin(2π *
    //                                   (nowMs - startMs) / periodMs +
    //                                   phaseRad)
    //
    // Result is rounded to long via static_cast (truncation toward zero).
    long valueAt(unsigned long nowMs) const;

private:
    int _amplitude;
    int _offset;
    unsigned long _periodMs;
    double _phaseRad;
    unsigned long _startMs;
    bool _started;
};

#endif  // DIGIMOTION_MOTION_SINEOSCILLATOR_H
