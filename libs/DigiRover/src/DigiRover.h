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
 *   ROVER_DC_MOTOR_4PIN  : 2 DcMotorChannel motors, each owning 2 GPIO
 *                          pins (forward + reverse) for an integrated
 *                          H-bridge. target=±speed%, DcMotorChannel
 *                          handles direction via sign internally.
 *                          (Phase X-1.5 Q-D=A refactor: was 4 single-
 *                          direction IActuatorChannel slots, now 2
 *                          DcMotorChannel complete H-bridge motors.)
 *
 * Lifecycle: setMode-equivalent is the init*Mode() call. Re-init switches
 * mode and rebuilds the channel registry.
 */

#ifndef DIGIROVER_H
#define DIGIROVER_H

#include <actuator/IActuatorChannel.h>
#include <actuator/DcMotorChannel.h>
#include <motion/ChannelBank.h>
#include <trim/ITrimStore.h>

class DigiRover {
public:
    enum RoverMode {
        ROVER_UNSET          = 0,
        ROVER_SERVO_2PIN     = 1,
        ROVER_DC_MOTOR_4PIN  = 2
    };

    // Channel index slots. Both modes use only the first 2 slots; the
    // _channels[] array is sized to MAX_CHANNELS=4 for defensive bounds
    // checking (and to match the array shape from before the Phase X-1.5
    // Q-D=A refactor, when DC motor mode used 4 separate single-direction
    // channels).
    enum ChannelIndex {
        LEFT_SERVO      = 0,
        RIGHT_SERVO     = 1,
        LEFT_DC_MOTOR   = 0,  // aliases LEFT_SERVO (both modes share slot 0)
        RIGHT_DC_MOTOR  = 1,  // aliases RIGHT_SERVO
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

    // Phase X-1.5 Q-D=A refactor: takes 2 DcMotorChannel pointers (1 per
    // motor, each owning forward + reverse pins internally) instead of 4
    // single-direction IActuatorChannel pointers. Direction is signaled by
    // sign of setTarget within each DcMotorChannel's _writeHw, eliminating
    // the previous _setHBridgeSide helper.
    bool initDcMotorMode(DcMotorChannel* left, DcMotorChannel* right) {
        if (left == nullptr || right == nullptr) return false;
        _resetChannels();
        _channels[LEFT_DC_MOTOR]  = left;
        _channels[RIGHT_DC_MOTOR] = right;
        _channelCount = 2;
        _mode = ROVER_DC_MOTOR_4PIN;
        _bank.addChannel(left);
        _bank.addChannel(right);
        left->attach();
        right->attach();
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
    // Phase X-1.5 Q-D=A refactor: trim is per-motor (deadband %), keyed
    // by the motor's forward pin (the conventional "primary" pin
    // identifier).
    bool initDcMotorModeWithTrim(ITrimStore& store,
                                 DcMotorChannel* left, DcMotorChannel* right,
                                 int pinLeftForward, int pinRightForward) {
        if (!initDcMotorMode(left, right)) return false;
        store.applyToChannel(_channels[LEFT_DC_MOTOR],  pinLeftForward);
        store.applyToChannel(_channels[RIGHT_DC_MOTOR], pinRightForward);
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
    // Phase 3-A/B Session 156: per-channel reverse forward. Works for both
    // servo mode (ContinuousServoChannel) and dc motor mode (DcMotorChannel)
    // — both concrete IActuatorChannel override setReverse. Out-of-range idx
    // silently no-ops, mirroring setChannelTrim semantics.
    void setChannelReverse(int idx, bool reverse) {
        IActuatorChannel* ch = channelAt(idx);
        if (ch != nullptr) ch->setReverse(reverse);
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
            // DcMotorChannel handles direction via sign of target internally.
            // Session 159: the right motor is sign-mirrored here, exactly like
            // servo mode above — both wheels are mounted facing each other on
            // a differential-drive chassis, so robot-forward = left shaft
            // forward + right shaft reverse. Without this, forward() spun the
            // bot in place on a standard mirror-mounted DC chassis. Users with
            // a non-mirrored wiring can flip it back via setChannelReverse.
            if (_channels[LEFT_DC_MOTOR] != nullptr)
                _channels[LEFT_DC_MOTOR]->setTarget(leftSigned);
            if (_channels[RIGHT_DC_MOTOR] != nullptr)
                _channels[RIGHT_DC_MOTOR]->setTarget(-rightSigned);
        }
        _isMoving = (leftSigned != 0) || (rightSigned != 0);
    }
};

#endif  // DIGIROVER_H
