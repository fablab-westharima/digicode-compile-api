/*
 * DigiMotion - DcMotorChannel (Layer 2, LEDC dual-pin H-bridge)
 * Copyright (C) 2026 DigiCo LLC
 * Licensed under AGPL-3.0-or-later. Original design (not OttoDIY-derived).
 *
 * Brushed DC motor driven by a 2-pin H-bridge (e.g. TB6612FNG, L9110S,
 * DRV8833). Each direction uses a separate ESP32 LEDC PWM channel. target
 * is -100..+100 % velocity; trim is deadband compensation (%), boosting
 * the magnitude so the motor actually moves out of static friction at
 * low command values.
 *
 * pulse range: no-op (DC motor does not consume PWM pulse range; the
 * setting refers to servo µs).
 * setMaxRate(percentPerSec): acceleration ramp via the pump scheduler.
 * setTrim(percentDeadband): added to the |velocity| magnitude before
 * writing the PWM duty, capped at 100 %.
 *
 * _lastWrittenHw encodes the signed duty in the range -255..+255 so tests
 * can distinguish direction without needing the LEDC pin state.
 */

#ifndef DIGIMOTION_ACTUATOR_DCMOTORCHANNEL_H
#define DIGIMOTION_ACTUATOR_DCMOTORCHANNEL_H

#include "IActuatorChannel.h"

#ifdef ARDUINO_ARCH_ESP32
  #include <Arduino.h>  // ledcAttach / ledcWrite / ledcDetach (v3+ API)
#endif

class DcMotorChannel : public IActuatorChannel {
public:
    static constexpr int VELOCITY_MIN = -100;
    static constexpr int VELOCITY_MAX = 100;
    static constexpr int TRIM_MIN = 0;       // deadband is non-negative
    static constexpr int TRIM_MAX = 30;
    static constexpr int DUTY_MAX = 255;     // 8-bit PWM
    static constexpr int PWM_FREQ_HZ = 5000;
    static constexpr int PWM_RES_BITS = 8;

    // forwardPin drives motor forward when written; reversePin for reverse.
    DcMotorChannel(int forwardPin, int reversePin)
        : _forwardPin(forwardPin), _reversePin(reversePin) {}

    bool attach() override {
#ifdef ARDUINO_ARCH_ESP32
        ledcAttach(_forwardPin, PWM_FREQ_HZ, PWM_RES_BITS);
        ledcAttach(_reversePin, PWM_FREQ_HZ, PWM_RES_BITS);
#endif
        _attached = true;
        _writeHw(_current);  // initial HW state = stored velocity (default 0 = brake)
        return true;
    }
    void detach() override {
#ifdef ARDUINO_ARCH_ESP32
        ledcWrite(_forwardPin, 0);
        ledcWrite(_reversePin, 0);
        ledcDetach(_forwardPin);
        ledcDetach(_reversePin);
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

    void setPulseRange(int /*minUs*/, int /*maxUs*/) override {}
    void setMaxRate(int percentPerSec) override { _maxRate = percentPerSec; }
    void setTrim(int deadbandPercent) override {
        if (deadbandPercent < TRIM_MIN) deadbandPercent = TRIM_MIN;
        if (deadbandPercent > TRIM_MAX) deadbandPercent = TRIM_MAX;
        _trim = deadbandPercent;
        // Runtime deadband adjustment: re-write duty so a running motor
        // visibly responds to trim change without waiting for next setTarget.
        if (_attached) _writeHw(_current);
    }
    void setReverse(bool reverse) override {
        _reverse = reverse;
        if (_attached) _writeHw(_current);
    }

    int getPulseMin() const override { return 0; }
    int getPulseMax() const override { return 0; }
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
    // Apply deadband-aware mapping and write to H-bridge pins.
    // sign(v) selected direction; |v| + trim (capped at 100) → duty.
    // Phase 3-A: reverse=true → velocity sign flip (= H-bridge forward/reverse
    // pin swap effect, no field reordering).
    virtual void _writeHw(int velocityPercent) {
        if (_reverse) velocityPercent = -velocityPercent;
        if (velocityPercent == 0) {
            _lastWrittenHw = 0;
#ifdef ARDUINO_ARCH_ESP32
            if (_attached) {
                ledcWrite(_forwardPin, 0);
                ledcWrite(_reversePin, 0);
            }
#endif
            return;
        }
        int magnitude = velocityPercent > 0 ? velocityPercent : -velocityPercent;
        magnitude += _trim;  // deadband boost
        if (magnitude > VELOCITY_MAX) magnitude = VELOCITY_MAX;
        int duty = (magnitude * DUTY_MAX) / VELOCITY_MAX;
        if (velocityPercent > 0) {
            _lastWrittenHw = duty;       // +duty = forward
#ifdef ARDUINO_ARCH_ESP32
            if (_attached) {
                ledcWrite(_reversePin, 0);
                ledcWrite(_forwardPin, duty);
            }
#endif
        } else {
            _lastWrittenHw = -duty;      // -duty = reverse
#ifdef ARDUINO_ARCH_ESP32
            if (_attached) {
                ledcWrite(_forwardPin, 0);
                ledcWrite(_reversePin, duty);
            }
#endif
        }
    }

    int _forwardPin;
    int _reversePin;
    int _maxRate = 0;
    int _trim = 0;
    bool _reverse = false;
    int _target = 0;
    int _current = 0;
    int _lastWrittenHw = 0;
    unsigned long _lastStepMs = 0;
    bool _attached = false;
};

#endif // DIGIMOTION_ACTUATOR_DCMOTORCHANNEL_H
