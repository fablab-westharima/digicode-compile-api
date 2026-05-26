/*
 * DigiMotion - StepperPollChannel (Layer 2, AccelStepper-backed)
 * Copyright (C) 2026 DigiCo LLC
 * Licensed under AGPL-3.0-or-later. Original design (not OttoDIY-derived).
 *
 * Polling stepper: AccelStepper::run() is called every pump() tick to
 * advance one micro-step toward the target. CPU-bound (~1 kHz max step
 * rate). Use StepperHwChannel for higher rates that need HW peripheral
 * driving.
 *
 * Supports DRIVER mode (a4988-style, 2 pins: STEP + DIR) and FULL4WIRE mode
 * (ULN2003 + 28BYJ-48, 4 coil pins). Other AccelStepper modes can be added
 * later; the two above cover all current DigiCode stepper blocks.
 *
 * pulse range: no-op (steppers do not consume PWM pulse range).
 * setMaxRate(stepsPerSec): forwarded to AccelStepper::setMaxSpeed().
 * setTrim(steps): added to the target when forwarded to moveTo() and
 *   captured in _lastWrittenHw, so home-position calibration is observable
 *   on host without HW.
 */

#ifndef DIGIMOTION_ACTUATOR_STEPPERPOLLCHANNEL_H
#define DIGIMOTION_ACTUATOR_STEPPERPOLLCHANNEL_H

#include "IActuatorChannel.h"

#ifdef ARDUINO_ARCH_ESP32
  #include <AccelStepper.h>
#endif

class StepperPollChannel : public IActuatorChannel {
public:
    // AccelStepper MotorInterfaceType values.
    static constexpr int MODE_DRIVER = 1;
    static constexpr int MODE_FULL4WIRE = 4;

    // DRIVER mode: stepPin + dirPin.
    StepperPollChannel(int stepPin, int dirPin)
        : _mode(MODE_DRIVER), _p1(stepPin), _p2(dirPin), _p3(-1), _p4(-1)
#ifdef ARDUINO_ARCH_ESP32
        , _stepper(MODE_DRIVER, stepPin, dirPin)
#endif
    {}

    // DRIVER mode with enable pin (Phase X-1.5 Q-G=ζ).
    // Most A4988 / DRV8825 drivers have an EN pin that must be wired to
    // assert active-low for the driver to source coil current. Passing
    // enablePin < 0 is identical to the 2-arg ctor (no enable wiring).
    // AccelStepper's enableOutputs() is called in attach() and will drive
    // this pin per its setPinsInverted polarity (default polarity = LOW
    // for enable = OFF; user wires for the driver's expected polarity).
    StepperPollChannel(int stepPin, int dirPin, int enablePin)
        : _mode(MODE_DRIVER), _p1(stepPin), _p2(dirPin), _p3(enablePin), _p4(-1)
#ifdef ARDUINO_ARCH_ESP32
        , _stepper(MODE_DRIVER, stepPin, dirPin)
#endif
    {
#ifdef ARDUINO_ARCH_ESP32
        if (enablePin >= 0) {
            _stepper.setEnablePin((uint8_t)enablePin);
        }
#endif
    }

    // FULL4WIRE mode: 4 coil pins (e.g. ULN2003 IN1..IN4 → user-wired order).
    StepperPollChannel(int pin1, int pin2, int pin3, int pin4)
        : _mode(MODE_FULL4WIRE), _p1(pin1), _p2(pin2), _p3(pin3), _p4(pin4)
#ifdef ARDUINO_ARCH_ESP32
        , _stepper(MODE_FULL4WIRE, pin1, pin3, pin2, pin4)  // AccelStepper coil order
#endif
    {}

    bool attach() override {
#ifdef ARDUINO_ARCH_ESP32
        _stepper.enableOutputs();
        if (_maxRate > 0) _stepper.setMaxSpeed((float)_maxRate);
        // Default acceleration: lib default is 1, which causes long ramps;
        // give it something usable for the typical hobby use until the user
        // sets max rate.
        _stepper.setAcceleration(200.0f);
#endif
        _attached = true;
#ifdef ARDUINO_ARCH_ESP32
        getBackgroundPump().registerPumpable(this);
#endif
        return true;
    }
    void detach() override {
#ifdef ARDUINO_ARCH_ESP32
        _stepper.disableOutputs();
        getBackgroundPump().unregisterPumpable(this);
#endif
        _attached = false;
    }
    bool isAttached() const override { return _attached; }

    // Phase 3-A: user-facing semantics. setTarget(N), getCurrent eventually
    // converges to N regardless of _reverse. Internally moveTo receives
    // (_reverse ? -_target : _target) + _trim. hasReachedTarget compares
    // internal distanceToGo so it remains correct under both modes.
    void setTarget(long target) override {
        _target = target;
#ifdef ARDUINO_ARCH_ESP32
        if (_attached) {
            long effective = _reverse ? -_target : _target;
            _stepper.moveTo(effective + _trim);
        }
#endif
    }
    long getTarget() const override { return _target; }
    long getCurrent() const override {
#ifdef ARDUINO_ARCH_ESP32
        if (_attached) {
            long internal = _stepper.currentPosition();
            return _reverse ? -internal : internal;
        }
        return _current;
#else
        return _current;
#endif
    }
    bool hasReachedTarget() const override {
#ifdef ARDUINO_ARCH_ESP32
        return _attached ? (_stepper.distanceToGo() == 0) : (_current == _target);
#else
        return _current == _target;
#endif
    }

    // pulse range: no-op (steppers do not consume PWM pulse range).
    void setPulseRange(int /*minUs*/, int /*maxUs*/) override {}
    void setMaxRate(int stepsPerSec) override {
        _maxRate = stepsPerSec;
#ifdef ARDUINO_ARCH_ESP32
        if (_attached && _maxRate > 0) _stepper.setMaxSpeed((float)_maxRate);
#endif
    }
    void setTrim(int offsetSteps) override { _trim = offsetSteps; }
    void setReverse(bool reverse) override {
        _reverse = reverse;
#ifdef ARDUINO_ARCH_ESP32
        if (_attached) {
            long effective = _reverse ? -_target : _target;
            _stepper.moveTo(effective + _trim);
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

    void pump(unsigned long /*nowMs*/) override {
        if (!_attached) return;
#ifdef ARDUINO_ARCH_ESP32
        _stepper.run();
        long internalPos = _stepper.currentPosition();
        _current = _reverse ? -internalPos : internalPos;
#else
        // Host (no HW): when there is work pending, jump current to target so
        // the rest of the pump scheduling logic (isActive returns false after)
        // is exercised correctly by tests. _current is user-facing, so it
        // matches _target directly regardless of _reverse.
        _current = _target;
#endif
        // _lastWrittenHw captures the value sent to moveTo (internal coord)
        // so tests can verify the direction sign was actually flipped on HW.
        long internalWritten = (_reverse ? -_target : _target) + _trim;
        _lastWrittenHw = (int)internalWritten;
    }

protected:
    int _mode;
    int _p1, _p2, _p3, _p4;
    int _maxRate = 0;
    int _trim = 0;
    bool _reverse = false;
    long _target = 0;
    long _current = 0;
    int _lastWrittenHw = 0;
    bool _attached = false;

#ifdef ARDUINO_ARCH_ESP32
    // mutable: const-correctness adapter for non-const AccelStepper query
    // methods (currentPosition() / distanceToGo()) called from const member
    // functions (getCurrent() / hasReachedTarget(), IActuatorChannel virtual
    // override contract). AccelStepper lib does not declare these query
    // methods const despite their read-only semantics, so we mark the field
    // mutable to satisfy the const member function's discard-qualifier
    // requirement without changing the IActuatorChannel virtual signature
    // (which would cascade to all 6 concrete channel classes). StepperHwChannel
    // uses pointer field (FastAccelStepper*) and does not need this adapter
    // (pointer-to-non-const is mutable inside const member by C++ rules).
    //
    // Bug source: Session 154 X-6 1000-case orchestrator at 250/1000 = 17 fail,
    // all m5stack-basic singleton, all stderr `passing 'const AccelStepper' as
    // 'this' argument discards qualifiers [-fpermissive]` at L102 + L109.
    // case 14 罠 7 度目 candidate (production smoke = audit 自己再帰 6 度目
    // production layer の追加で発覚): host pio test 128 PASS は ARDUINO_ARCH_ESP32
    // guard 外 native build で ESP32 branch を未 compile = const-correctness
    // violation を skip、 production ESP32 build で初発覚。 cluster audit (DigiMotion
    // lib 全 24 file) で同根なし = この 1 file 1 line fix で完結。
    mutable AccelStepper _stepper;
#endif
};

#endif // DIGIMOTION_ACTUATOR_STEPPERPOLLCHANNEL_H
