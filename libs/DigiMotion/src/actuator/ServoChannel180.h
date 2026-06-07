/*
 * DigiMotion - ServoChannel180 (Layer 2, 0..180° hobby servo)
 * Copyright (C) 2026 DigiCo LLC
 * Licensed under AGPL-3.0-or-later. Original design (not OttoDIY-derived).
 *
 * Layout: full inline impl in header, with #ifdef ARDUINO_ARCH_ESP32 guards
 * on (a) ESP32Servo.h include, (b) Servo _servo member, (c) HW call sites
 * inside attach/detach/_writeHw. Native (host) unit test env compiles the
 * class with HW members + calls elided so pure-logic state (3-axis settings,
 * target clamp, rate cap, pump scheduling, _lastWrittenHw) is observable.
 */

#ifndef DIGIMOTION_ACTUATOR_SERVOCHANNEL180_H
#define DIGIMOTION_ACTUATOR_SERVOCHANNEL180_H

#include "IActuatorChannel.h"

#ifdef ARDUINO_ARCH_ESP32
  #include <ESP32Servo.h>
#endif

class ServoChannel180 : public IActuatorChannel {
public:
    // ESP32Servo DEFAULT_uS_LOW/HIGH; matches stock Servo::attach(pin) default.
    static constexpr int DEFAULT_PULSE_MIN_US = 544;
    static constexpr int DEFAULT_PULSE_MAX_US = 2400;
    static constexpr int ANGLE_MIN = 0;
    static constexpr int ANGLE_MAX = 180;
    static constexpr int TRIM_MIN = -30;
    static constexpr int TRIM_MAX = 30;

    explicit ServoChannel180(int pin) : _pin(pin) {}

    // === lifecycle ===
    bool attach() override {
#ifdef ARDUINO_ARCH_ESP32
        _servo.attach(_pin, _pulseMin, _pulseMax);
#endif
        _attached = true;
        // Session 160 case 24: (re)attach resets so the first command re-establishes HW position.
        _positionKnown = false;
        _commanded = false;
        // Phase F-5 (Session 157、サーボピクつき真因 1 解消): boot 時 90° 強制移動廃止
        // = attach 直後の _writeHw(_current=90) 削除、 servo は pump 経路経由で初回 HW write。
        // 物理動作: boot 後 setTarget + pump tick まで servo 脱力 (= gear stress 最小、 ピクつき解消)。
        // user code 視点契約: 初回 setTarget 後 ~1ms (= pump tick interval) で HW 反映、 pre-Phase-F-5
        // の「attach 直後に _current 位置で固定」 とは異なる contract。 setTrim/setReverse/setPulseRange
        // 経由の直接 _writeHw path (= attached なら _writeHw(_current) 即書込) は preserve、 これらは
        // pump 経路通らない直接 HW reflection。
#ifdef ARDUINO_ARCH_ESP32
        getBackgroundPump().registerPumpable(this);
#endif
        return true;
    }

    void detach() override {
#ifdef ARDUINO_ARCH_ESP32
        _servo.detach();
        getBackgroundPump().unregisterPumpable(this);
#endif
        _attached = false;
    }

    bool isAttached() const override { return _attached; }

    // === target control ===
    void setTarget(long target) override {
        if (target < ANGLE_MIN) target = ANGLE_MIN;
        if (target > ANGLE_MAX) target = ANGLE_MAX;
        _target = (int)target;
        _commanded = true;  // Session 160 case 24: first command gates pump-driven HW establish
    }
    long getTarget() const override { return _target; }
    long getCurrent() const override { return _current; }
    bool hasReachedTarget() const override { return _positionKnown && _current == _target; }

    // === 3-axis ===
    void setPulseRange(int minUs, int maxUs) override {
        _pulseMin = minUs;
        _pulseMax = maxUs;
#ifdef ARDUINO_ARCH_ESP32
        if (_attached) {
            _servo.detach();
            _servo.attach(_pin, _pulseMin, _pulseMax);
        }
#endif
        if (_attached) _writeHw(_current);  // re-apply position after reattach
    }
    void setMaxRate(int degPerSec) override { _maxRate = degPerSec; }
    void setTrim(int offsetDeg) override {
        if (offsetDeg < TRIM_MIN) offsetDeg = TRIM_MIN;
        if (offsetDeg > TRIM_MAX) offsetDeg = TRIM_MAX;
        _trim = offsetDeg;
        // Apply trim change to HW immediately so runtime trim adjustment is
        // visible without waiting for next target change.
        if (_attached) _writeHw(_current);
    }
    void setReverse(bool reverse) override {
        _reverse = reverse;
        if (_attached) _writeHw(_current);
    }

    int getPulseMin() const override { return _pulseMin; }
    int getPulseMax() const override { return _pulseMax; }
    int getMaxRate() const override { return _maxRate; }
    int getTrim() const override { return _trim; }
    bool getReverse() const override { return _reverse; }
    int getLastWrittenHw() const override { return _lastWrittenHw; }

    // === IPumpable ===
    bool isActive() const override { return _attached && _commanded && (!_positionKnown || _current != _target); }

    void pump(unsigned long nowMs) override {
        if (!_attached) return;
        if (_current == _target) {
            // Session 160 case 24: Phase F-5 dropped the attach-time write, so a
            // first command landing on the default value (home→90) would never
            // reach HW. Force one write to establish position, then idle.
            if (!_positionKnown) _writeHw(_current);
            return;
        }
        if (_maxRate > 0) {
            int stepMs = 1000 / _maxRate;
            if (stepMs < 1) stepMs = 1;
            if (nowMs - _lastStepMs < (unsigned long)stepMs) return;
            _current += (_current < _target) ? 1 : -1;
            _lastStepMs = nowMs;
        } else {
            _current = _target;
        }
        _writeHw(_current);
    }

protected:
    // virtual so test code MAY override; default impl applies reverse mirror,
    // then trim + clamp + (on ESP32) Servo::write. Captures the final value
    // in _lastWrittenHw regardless of platform.
    // Phase 3-A: reverse mirror around mid-point (180 - valueDeg) compensates
    // for physically inverted servo mounting. Applied BEFORE trim so trim
    // remains a user-facing offset on the user-facing angle.
    virtual void _writeHw(int valueDeg) {
        _positionKnown = true;  // Session 160 case 24: any HW write establishes position
        if (_reverse) valueDeg = ANGLE_MAX - valueDeg;
        int trimmed = valueDeg + _trim;
        if (trimmed < ANGLE_MIN) trimmed = ANGLE_MIN;
        if (trimmed > ANGLE_MAX) trimmed = ANGLE_MAX;
        _lastWrittenHw = trimmed;
#ifdef ARDUINO_ARCH_ESP32
        if (_attached) _servo.write(trimmed);
#endif
    }

    int _pin;
    int _pulseMin = DEFAULT_PULSE_MIN_US;
    int _pulseMax = DEFAULT_PULSE_MAX_US;
    int _maxRate = 0;       // 0 = unlimited
    int _trim = 0;
    bool _reverse = false;
    int _target = 90;
    int _current = 90;
    bool _positionKnown = false;  // Session 160 case 24: HW written >=1 since attach
    bool _commanded = false;      // setTarget called >=1 since attach
    int _lastWrittenHw = -1;
    unsigned long _lastStepMs = 0;
    bool _attached = false;

#ifdef ARDUINO_ARCH_ESP32
    Servo _servo;
#endif
};

#endif // DIGIMOTION_ACTUATOR_SERVOCHANNEL180_H
