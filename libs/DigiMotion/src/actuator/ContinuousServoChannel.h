/*
 * DigiMotion - ContinuousServoChannel (Layer 2, continuous-rotation servo)
 * Copyright (C) 2026 DigiCo LLC
 * Licensed under AGPL-3.0-or-later. Original design (not OttoDIY-derived).
 *
 * Continuous-rotation servos are velocity-controlled via the same PWM
 * interface as positional servos: PWM at the lib's stop-center => no motion,
 * PWM at pulseMin => full reverse, PWM at pulseMax => full forward. We
 * expose a -100..+100 % velocity target and map it to a 0..180 angle that
 * the underlying Servo lib drives. trim is a degree offset on the stop
 * center to compensate for servo neutral drift (the common reason
 * continuous servos creep at "stop"). maxRate is %/sec acceleration.
 */

#ifndef DIGIMOTION_ACTUATOR_CONTINUOUSSERVOCHANNEL_H
#define DIGIMOTION_ACTUATOR_CONTINUOUSSERVOCHANNEL_H

#include "IActuatorChannel.h"

#ifdef ARDUINO_ARCH_ESP32
  #include <ESP32Servo.h>
#endif

class ContinuousServoChannel : public IActuatorChannel {
public:
    static constexpr int DEFAULT_PULSE_MIN_US = 1000;
    static constexpr int DEFAULT_PULSE_MAX_US = 2000;
    static constexpr int VELOCITY_MIN = -100;
    static constexpr int VELOCITY_MAX = 100;
    static constexpr int TRIM_MIN = -30;
    static constexpr int TRIM_MAX = 30;
    static constexpr int STOP_DEG = 90;
    static constexpr int DEG_MIN = 0;
    static constexpr int DEG_MAX = 180;

    explicit ContinuousServoChannel(int pin) : _pin(pin) {}

    bool attach() override {
#ifdef ARDUINO_ARCH_ESP32
        _servo.attach(_pin, _pulseMin, _pulseMax);
#endif
        _attached = true;
        // Phase F-5 (Session 157、サーボピクつき真因 1 解消): attach 直後の _writeHw(_current=0
        // = stop center) 削除、 motor は pump 経路経由で初回 HW write。 連続回転 servo の場合 boot
        // 時 servo 脱力 = 慣性で停止 (= pre-Phase-F-5 では強制 90° pulse で center brake)、 user code
        // setTarget(0) + pump で center brake が pump 経由で発動。
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
        if (target < VELOCITY_MIN) target = VELOCITY_MIN;
        if (target > VELOCITY_MAX) target = VELOCITY_MAX;
        _target = (int)target;
    }
    long getTarget() const override { return _target; }
    long getCurrent() const override { return _current; }
    bool hasReachedTarget() const override { return _current == _target; }

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
    void setMaxRate(int percentPerSec) override { _maxRate = percentPerSec; }
    void setTrim(int offsetDeg) override {
        if (offsetDeg < TRIM_MIN) offsetDeg = TRIM_MIN;
        if (offsetDeg > TRIM_MAX) offsetDeg = TRIM_MAX;
        _trim = offsetDeg;
        // Apply trim shift immediately; stop center moves so even an idle
        // (target=0) motor needs a fresh PWM to land on the new center.
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

    bool isActive() const override { return _attached && _current != _target; }

    void pump(unsigned long nowMs) override {
        if (!_attached) return;
        if (_current == _target) return;
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
    // Map velocity (-100..+100 %) to PWM angle (0..180), with stop center
    // shifted by trim deg.
    //   velocity =    0 → angle = STOP_DEG + trim
    //   velocity = +100 → angle = DEG_MAX  (forward, ignoring trim for full)
    //   velocity = -100 → angle = DEG_MIN  (reverse, ignoring trim for full)
    // Phase 3-A: reverse=true → velocity sign flip BEFORE PWM mapping.
    virtual void _writeHw(int velocityPercent) {
        if (_reverse) velocityPercent = -velocityPercent;
        int center = STOP_DEG + _trim;
        int span = (velocityPercent >= 0) ? (DEG_MAX - center) : (center - DEG_MIN);
        int angle = center + (velocityPercent * span) / VELOCITY_MAX;
        if (angle < DEG_MIN) angle = DEG_MIN;
        if (angle > DEG_MAX) angle = DEG_MAX;
        _lastWrittenHw = angle;
#ifdef ARDUINO_ARCH_ESP32
        if (_attached) _servo.write(angle);
#endif
    }

    int _pin;
    int _pulseMin = DEFAULT_PULSE_MIN_US;
    int _pulseMax = DEFAULT_PULSE_MAX_US;
    int _maxRate = 0;
    int _trim = 0;
    bool _reverse = false;
    int _target = 0;        // 0 % = stop
    int _current = 0;
    int _lastWrittenHw = -1;
    unsigned long _lastStepMs = 0;
    bool _attached = false;

#ifdef ARDUINO_ARCH_ESP32
    Servo _servo;
#endif
};

#endif // DIGIMOTION_ACTUATOR_CONTINUOUSSERVOCHANNEL_H
