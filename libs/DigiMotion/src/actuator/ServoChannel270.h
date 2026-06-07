/*
 * DigiMotion - ServoChannel270 (Layer 2, 0..270° wide-range servo)
 * Copyright (C) 2026 DigiCo LLC
 * Licensed under AGPL-3.0-or-later. Original design (not OttoDIY-derived).
 *
 * Same structure as ServoChannel180; the difference is ANGLE_MAX=270 (servos
 * such as ASMC-04B / DSSERVO DS3225). The PWM-vs-angle mapping is handled by
 * the underlying Servo lib when attach(pin, min, max) is given the wider
 * pulse range for a 270° servo.
 */

#ifndef DIGIMOTION_ACTUATOR_SERVOCHANNEL270_H
#define DIGIMOTION_ACTUATOR_SERVOCHANNEL270_H

#include "IActuatorChannel.h"

#ifdef ARDUINO_ARCH_ESP32
  #include <ESP32Servo.h>
#endif

class ServoChannel270 : public IActuatorChannel {
public:
    static constexpr int DEFAULT_PULSE_MIN_US = 500;
    static constexpr int DEFAULT_PULSE_MAX_US = 2500;
    static constexpr int ANGLE_MIN = 0;
    static constexpr int ANGLE_MAX = 270;
    static constexpr int TRIM_MIN = -30;
    static constexpr int TRIM_MAX = 30;

    explicit ServoChannel270(int pin) : _pin(pin) {}

    bool attach() override {
#ifdef ARDUINO_ARCH_ESP32
        _servo.attach(_pin, _pulseMin, _pulseMax);
#endif
        _attached = true;
        // Session 160 case 24: (re)attach resets so the first command re-establishes HW position.
        _positionKnown = false;
        _commanded = false;
        // Phase F-5 (Session 157、サーボピクつき真因 1 解消): 同 ServoChannel180 = attach 直後の
        // _writeHw(_current=135) 削除、 servo は pump 経路経由で初回 HW write。
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

    void setTarget(long target) override {
        if (target < ANGLE_MIN) target = ANGLE_MIN;
        if (target > ANGLE_MAX) target = ANGLE_MAX;
        _target = (int)target;
        _commanded = true;  // Session 160 case 24: first command gates pump-driven HW establish
    }
    long getTarget() const override { return _target; }
    long getCurrent() const override { return _current; }
    bool hasReachedTarget() const override { return _positionKnown && _current == _target; }

    void setPulseRange(int minUs, int maxUs) override {
        _pulseMin = minUs;
        _pulseMax = maxUs;
#ifdef ARDUINO_ARCH_ESP32
        if (_attached) {
            _servo.detach();
            _servo.attach(_pin, _pulseMin, _pulseMax);
        }
#endif
        if (_attached) _writeHw(_current);
    }
    void setMaxRate(int degPerSec) override { _maxRate = degPerSec; }
    void setTrim(int offsetDeg) override {
        if (offsetDeg < TRIM_MIN) offsetDeg = TRIM_MIN;
        if (offsetDeg > TRIM_MAX) offsetDeg = TRIM_MAX;
        _trim = offsetDeg;
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

    bool isActive() const override { return _attached && _commanded && (!_positionKnown || _current != _target); }

    void pump(unsigned long nowMs) override {
        if (!_attached) return;
        if (_current == _target) {
            // Session 160 case 24: Phase F-5 dropped the attach-time write, so a
            // first command landing on the default value would never reach HW.
            // Force one write to establish position, then idle.
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
    // Phase 3-A: reverse mirror around ANGLE_MAX (270 - valueDeg) compensates
    // for physically inverted 270° servo mounting. Applied BEFORE trim.
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
    int _maxRate = 0;
    int _trim = 0;
    bool _reverse = false;
    int _target = 135;       // mid-range for 270° servo
    int _current = 135;
    bool _positionKnown = false;  // Session 160 case 24: HW written >=1 since attach
    bool _commanded = false;      // setTarget called >=1 since attach
    int _lastWrittenHw = -1;
    unsigned long _lastStepMs = 0;
    bool _attached = false;

#ifdef ARDUINO_ARCH_ESP32
    Servo _servo;
#endif
};

#endif // DIGIMOTION_ACTUATOR_SERVOCHANNEL270_H
