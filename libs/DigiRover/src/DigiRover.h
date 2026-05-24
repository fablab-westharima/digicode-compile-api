/*
 * DigiRover - Layer 5 wheeled rover API (dual-mode: 2-pin continuous
 *             servo / 4-pin H-bridge DC motor)
 * Copyright (C) 2026 DigiCo LLC
 *
 * Licensed under the GNU Affero General Public License version 3 or later.
 * See LICENSE file for full terms.
 *
 * Origin declaration: original design. Not derived from OttoDIY/OttoDIYLib
 * (GPL-3.0) or any other GPL/AGPL upstream. T5 anti-derivation discipline:
 *   - The legacy `wheel_*` blocks had a 4-pin DC motor mode declared in
 *     source but unreachable from the Blockly UI. DigiRover exposes both
 *     modes as first-class init paths (initServoMode / initDcMotorMode),
 *     with mutually-exclusive selection and per-mode forward/backward/
 *     turn/spin command mapping. None of the legacy lib's helper
 *     signatures (`_setSpeed(int, int)`, `_moveAt(int, int)`, etc.) are
 *     reproduced.
 *   - Velocity commands (forward / backward / turnLeft / turnRight /
 *     spinLeft / spinRight / stop) are common rover vocabulary; the
 *     channel-target mapping per mode is original.
 *
 * Platform abstraction: header-inline.
 *
 * Mode contract (mutually exclusive):
 *   ROVER_SERVO_2PIN     : 2 continuous-rotation servos, target=±speed%
 *                          (left forward = +speed, right forward =
 *                          −speed mirrored).
 *   ROVER_DC_MOTOR_4PIN  : 4 H-bridge motor channels (LA/LB/RA/RB).
 *                          Forward on a side = A=+speed, B=0.
 *                          Backward on a side = A=0, B=+speed.
 *
 * Lifecycle: setMode-equivalent is the init*Mode() call. Re-init switches
 * mode and rebuilds the channel registry.
 */

#ifndef DIGIROVER_H
#define DIGIROVER_H

#include <actuator/IActuatorChannel.h>
#include <motion/ChannelBank.h>
#include <trim/ITrimStore.h>

class DigiRover {
public:
    enum RoverMode {
        ROVER_UNSET          = 0,
        ROVER_SERVO_2PIN     = 1,
        ROVER_DC_MOTOR_4PIN  = 2
    };

    // Channel index slots (sparse — only the first N are populated per mode).
    enum ChannelIndex {
        LEFT_SERVO      = 0,
        RIGHT_SERVO     = 1,
        LEFT_DC_A       = 0,
        LEFT_DC_B       = 1,
        RIGHT_DC_A      = 2,
        RIGHT_DC_B      = 3,
        MAX_CHANNELS    = 4
    };

    DigiRover() : _mode(ROVER_UNSET), _channelCount(0), _isMoving(false) {
        for (int i = 0; i < MAX_CHANNELS; ++i) _channels[i] = nullptr;
    }

    // === lifecycle ===
    bool initServoMode(IActuatorChannel* left, IActuatorChannel* right) {
        if (left == nullptr || right == nullptr) return false;
        _resetChannels();
        _channels[LEFT_SERVO]  = left;
        _channels[RIGHT_SERVO] = right;
        _channelCount = 2;
        _mode = ROVER_SERVO_2PIN;
        _bank.addChannel(left);
        _bank.addChannel(right);
        left->attach();
        right->attach();
        return true;
    }

    bool initDcMotorMode(IActuatorChannel* la, IActuatorChannel* lb,
                         IActuatorChannel* ra, IActuatorChannel* rb) {
        if (la == nullptr || lb == nullptr ||
            ra == nullptr || rb == nullptr) return false;
        _resetChannels();
        _channels[LEFT_DC_A]  = la;
        _channels[LEFT_DC_B]  = lb;
        _channels[RIGHT_DC_A] = ra;
        _channels[RIGHT_DC_B] = rb;
        _channelCount = 4;
        _mode = ROVER_DC_MOTOR_4PIN;
        _bank.addChannel(la);
        _bank.addChannel(lb);
        _bank.addChannel(ra);
        _bank.addChannel(rb);
        la->attach();
        lb->attach();
        ra->attach();
        rb->attach();
        return true;
    }

    // initWithTrim variants: apply trim from store per pin after init.
    bool initServoModeWithTrim(ITrimStore& store,
                               IActuatorChannel* left, IActuatorChannel* right,
                               int pinLeft, int pinRight) {
        if (!initServoMode(left, right)) return false;
        store.applyToChannel(_channels[LEFT_SERVO],  pinLeft);
        store.applyToChannel(_channels[RIGHT_SERVO], pinRight);
        return true;
    }
    bool initDcMotorModeWithTrim(ITrimStore& store,
                                 IActuatorChannel* la, IActuatorChannel* lb,
                                 IActuatorChannel* ra, IActuatorChannel* rb,
                                 int pinLA, int pinLB,
                                 int pinRA, int pinRB) {
        if (!initDcMotorMode(la, lb, ra, rb)) return false;
        store.applyToChannel(_channels[LEFT_DC_A],  pinLA);
        store.applyToChannel(_channels[LEFT_DC_B],  pinLB);
        store.applyToChannel(_channels[RIGHT_DC_A], pinRA);
        store.applyToChannel(_channels[RIGHT_DC_B], pinRB);
        return true;
    }

    int channelCount() const   { return _channelCount; }
    RoverMode mode() const     { return _mode; }
    IActuatorChannel* channelAt(int idx) const {
        if (idx < 0 || idx >= MAX_CHANNELS) return nullptr;
        return _channels[idx];
    }

    // === per-channel 3-axis ===
    void setChannelPulseRange(int idx, int minUs, int maxUs) {
        IActuatorChannel* ch = channelAt(idx);
        if (ch != nullptr) ch->setPulseRange(minUs, maxUs);
    }
    void setChannelMaxRate(int idx, int unitsPerSec) {
        IActuatorChannel* ch = channelAt(idx);
        if (ch != nullptr) ch->setMaxRate(unitsPerSec);
    }
    void setChannelTrim(int idx, int trimDeg) {
        IActuatorChannel* ch = channelAt(idx);
        if (ch != nullptr) ch->setTrim(trimDeg);
    }

    // === velocity commands ===
    // speedPercent in [0, 100]. Negative clamps to 0. >100 clamps to 100.
    void forward(int speedPercent)    { _drive(_clampSpeed(speedPercent),  _clampSpeed(speedPercent)); }
    void backward(int speedPercent)   { _drive(-_clampSpeed(speedPercent), -_clampSpeed(speedPercent)); }
    void turnLeft(int speedPercent)   { _drive(_clampSpeed(speedPercent) / 2, _clampSpeed(speedPercent)); }
    void turnRight(int speedPercent)  { _drive(_clampSpeed(speedPercent), _clampSpeed(speedPercent) / 2); }
    void spinLeft(int speedPercent)   { _drive(-_clampSpeed(speedPercent), _clampSpeed(speedPercent)); }
    void spinRight(int speedPercent)  { _drive(_clampSpeed(speedPercent), -_clampSpeed(speedPercent)); }
    void stop()                       { _drive(0, 0); }

    bool isMoving() const { return _isMoving; }

private:
    ChannelBank _bank;
    IActuatorChannel* _channels[MAX_CHANNELS];
    RoverMode _mode;
    int _channelCount;
    bool _isMoving;

    static int _clampSpeed(int s) {
        if (s < 0) return 0;
        if (s > 100) return 100;
        return s;
    }

    void _resetChannels() {
        // Rebuild the bank for re-init. The new bank inherits no
        // channels from the prior mode. Existing channel pointers
        // (caller-owned) are not detached here — caller manages.
        _bank = ChannelBank();
        for (int i = 0; i < MAX_CHANNELS; ++i) _channels[i] = nullptr;
        _channelCount = 0;
        _isMoving = false;
    }

    // Per-mode velocity mapping. leftSigned and rightSigned are signed
    // speeds in [-100, +100] (sign = direction).
    void _drive(int leftSigned, int rightSigned) {
        if (_mode == ROVER_SERVO_2PIN) {
            // Continuous-rotation servo: left forward = +speed,
            // right forward = -speed (mirrored mounting).
            if (_channels[LEFT_SERVO] != nullptr)
                _channels[LEFT_SERVO]->setTarget(leftSigned);
            if (_channels[RIGHT_SERVO] != nullptr)
                _channels[RIGHT_SERVO]->setTarget(-rightSigned);
        } else if (_mode == ROVER_DC_MOTOR_4PIN) {
            // H-bridge: A pin carries forward magnitude, B pin carries
            // backward magnitude. Only one is active per side at a time.
            _setHBridgeSide(_channels[LEFT_DC_A], _channels[LEFT_DC_B],   leftSigned);
            _setHBridgeSide(_channels[RIGHT_DC_A], _channels[RIGHT_DC_B], rightSigned);
        }
        _isMoving = (leftSigned != 0) || (rightSigned != 0);
    }

    static void _setHBridgeSide(IActuatorChannel* a, IActuatorChannel* b,
                                int signedSpeed) {
        if (a == nullptr || b == nullptr) return;
        if (signedSpeed >= 0) {
            a->setTarget(signedSpeed);
            b->setTarget(0);
        } else {
            a->setTarget(0);
            b->setTarget(-signedSpeed);
        }
    }
};

#endif  // DIGIROVER_H
