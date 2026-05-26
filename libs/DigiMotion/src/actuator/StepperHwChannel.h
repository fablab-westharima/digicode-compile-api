/*
 * DigiMotion - StepperHwChannel (Layer 2, FastAccelStepper-backed, D9)
 * Copyright (C) 2026 DigiCo LLC
 * Licensed under AGPL-3.0-or-later. Original design (not OttoDIY-derived).
 *
 * HW-peripheral stepper: FastAccelStepper drives the STEP pin via ESP32
 * RMT / MCPWM / PCNT, achieving ~200 kHz step rates without CPU load.
 * pump() does not generate steps; it only mirrors currentPosition() into
 * _current for diagnostic / isActive() purposes.
 *
 * License + maintenance + API + deps verified per rule 15 Section A
 * (60.md §8); FastAccelStepper MIT @ ^0.32, AGPL-compatible, RP2040
 * supported (D-new-2 future portability bonus).
 *
 * pulse range: no-op. setMaxRate(stepsPerSec) → setSpeedInHz.
 * setTrim(steps) added to moveTo target and captured in _lastWrittenHw.
 */

#ifndef DIGIMOTION_ACTUATOR_STEPPERHWCHANNEL_H
#define DIGIMOTION_ACTUATOR_STEPPERHWCHANNEL_H

#include "IActuatorChannel.h"

#ifdef ARDUINO_ARCH_ESP32
  #include <FastAccelStepper.h>
#endif

class StepperHwChannel : public IActuatorChannel {
public:
    // stepPin + dirPin (enable pin optional; if -1 the lib does not auto-enable).
    StepperHwChannel(int stepPin, int dirPin, int enablePin = -1)
        : _stepPin(stepPin), _dirPin(dirPin), _enablePin(enablePin) {}

    bool attach() override {
#ifdef ARDUINO_ARCH_ESP32
        static FastAccelStepperEngine engine;
        static bool engineInit = false;
        if (!engineInit) { engine.init(); engineInit = true; }

        _stepper = engine.stepperConnectToPin(_stepPin);
        if (!_stepper) {
            _attached = false;
            return false;
        }
        _stepper->setDirectionPin(_dirPin);
        if (_enablePin >= 0) {
            _stepper->setEnablePin(_enablePin);
            _stepper->setAutoEnable(true);
        }
        if (_maxRate > 0) _stepper->setSpeedInHz(_maxRate);
        // Default acceleration: 2x speed (so the ramp is over ~1 sec). User
        // overrides this implicitly by changing maxRate.
        _stepper->setAcceleration(_maxRate > 0 ? (long)_maxRate * 2 : 1000);
#endif
        _attached = true;
#ifdef ARDUINO_ARCH_ESP32
        getBackgroundPump().registerPumpable(this);
#endif
        return true;
    }

    void detach() override {
#ifdef ARDUINO_ARCH_ESP32
        if (_stepper) {
            _stepper->forceStop();
            _stepper = nullptr;
        }
        getBackgroundPump().unregisterPumpable(this);
#endif
        _attached = false;
    }
    bool isAttached() const override { return _attached; }

    // Phase 3-A: same user-facing semantics as StepperPollChannel.
    // setTarget(N) → moveTo((_reverse ? -N : N) + trim); getCurrent reads
    // FastAccelStepper position and mirrors back if _reverse.
    void setTarget(long target) override {
        _target = target;
#ifdef ARDUINO_ARCH_ESP32
        if (_attached && _stepper) {
            long effective = _reverse ? -_target : _target;
            _stepper->moveTo(effective + _trim);
        }
#endif
    }
    long getTarget() const override { return _target; }
    long getCurrent() const override {
#ifdef ARDUINO_ARCH_ESP32
        if (_attached && _stepper) {
            long internal = _stepper->getCurrentPosition();
            return _reverse ? -internal : internal;
        }
#endif
        return _current;
    }
    bool hasReachedTarget() const override {
#ifdef ARDUINO_ARCH_ESP32
        if (_attached && _stepper) return !_stepper->isRunning();
#endif
        return _current == _target;
    }

    void setPulseRange(int /*minUs*/, int /*maxUs*/) override {}
    void setMaxRate(int stepsPerSec) override {
        _maxRate = stepsPerSec;
#ifdef ARDUINO_ARCH_ESP32
        if (_attached && _stepper && _maxRate > 0) {
            _stepper->setSpeedInHz(_maxRate);
            _stepper->setAcceleration((long)_maxRate * 2);
        }
#endif
    }
    void setTrim(int offsetSteps) override { _trim = offsetSteps; }
    void setReverse(bool reverse) override {
        _reverse = reverse;
#ifdef ARDUINO_ARCH_ESP32
        if (_attached && _stepper) {
            long effective = _reverse ? -_target : _target;
            _stepper->moveTo(effective + _trim);
        }
#endif
    }

    int getPulseMin() const override { return 0; }
    int getPulseMax() const override { return 0; }
    int getMaxRate() const override { return _maxRate; }
    int getTrim() const override { return _trim; }
    bool getReverse() const override { return _reverse; }
    int getLastWrittenHw() const override { return _lastWrittenHw; }

    bool isActive() const override {
        if (!_attached) return false;
        return !hasReachedTarget();
    }

    // HW peripheral drives the steps; pump() only updates _current for the
    // platform-agnostic state inspection (tests, isActive(), Layer 3 bank).
    void pump(unsigned long /*nowMs*/) override {
        if (!_attached) return;
#ifdef ARDUINO_ARCH_ESP32
        if (_stepper) {
            long internalPos = _stepper->getCurrentPosition();
            _current = _reverse ? -internalPos : internalPos;
        }
#else
        _current = _target;
#endif
        long internalWritten = (_reverse ? -_target : _target) + _trim;
        _lastWrittenHw = (int)internalWritten;
    }

protected:
    int _stepPin;
    int _dirPin;
    int _enablePin;
    int _maxRate = 0;
    int _trim = 0;
    bool _reverse = false;
    long _target = 0;
    long _current = 0;
    int _lastWrittenHw = 0;
    bool _attached = false;

#ifdef ARDUINO_ARCH_ESP32
    FastAccelStepper* _stepper = nullptr;
#endif
};

#endif // DIGIMOTION_ACTUATOR_STEPPERHWCHANNEL_H
