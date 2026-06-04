/*
 * DigiMorpher - Layer 5 transformable (walk/roll dual-mode) robot API
 * Copyright (C) 2026 DigiCo LLC
 *
 * Licensed under the GNU Affero General Public License version 3 or later.
 * See LICENSE file for full terms.
 *
 * Origin declaration: original design. Not derived from OttoDIY/OttoDIYLib
 * (GPL-3.0) or any other GPL/AGPL upstream. The full T5 12-item anti-
 * derivation discipline applies — see `plans/active/60_robotics-redesign-
 * implementation-plan.md` §4. Highlights for this lib:
 *   - The dual-mode walk/roll concept is generic robotics vocabulary;
 *     the mode state machine, the {hip, foot} 4-channel split, and the
 *     motion patterns are new designs. No Otto multi-servo helper
 *     identifiers or signatures (T5 §3 / §7), no Otto-style oscillateServos
 *     orchestration; per-channel SineOscillator + ChannelBank.setTargets
 *     coordination via this class's tick(nowMs).
 *   - Mode guard logic (walk-mode motions reject in roll mode and vice
 *     versa) is original — the legacy transform library exposed walk+roll
 *     motions in a flat namespace with no mode guard.
 *
 * Platform abstraction: header-inline. Same #ifdef ARDUINO_ARCH_ESP32
 * polling pattern as DigiBiped for blocking variants.
 *
 * Channel index (single source of truth, case 23 incident D defense):
 *   0 = LEFT_HIP    (rotates leg vertical ↔ horizontal during shift)
 *   1 = RIGHT_HIP
 *   2 = LEFT_FOOT   (ankle servo, gait control in walk mode; roll-
 *                    rotation in roll mode)
 *   3 = RIGHT_FOOT
 *
 * Lifecycle (D8 + Session 143 lifecycle case c):
 *   initWithTrim(store, pins): boot-time trim push per channel.
 *   setChannelTrim(idx, deg): runtime override forwarded to channel.
 *   Precedence = call-sequence's responsibility.
 */

#ifndef DIGIMORPHER_H
#define DIGIMORPHER_H

#include <actuator/IActuatorChannel.h>
#include <motion/ChannelBank.h>
#include <motion/SineOscillator.h>
#include <trim/ITrimStore.h>

#ifdef ARDUINO_ARCH_ESP32
  #include <Arduino.h>
#endif

class DigiMorpher {
public:
    enum ChannelIndex {
        LEFT_HIP      = 0,
        RIGHT_HIP     = 1,
        LEFT_FOOT     = 2,
        RIGHT_FOOT    = 3,
        CHANNEL_COUNT = 4
    };

    enum MorphMode {
        MORPH_WALK = 0,
        MORPH_ROLL = 1
    };

    enum MotionId {
        MOTION_IDLE = 0,
        MOTION_HOME,
        MOTION_SHIFT,
        MOTION_WALK,
        MOTION_TURN,
        MOTION_ROLL,
        MOTION_ROLL_ROTATE,
        MOTION_PUSHUP,
        MOTION_DANCE
    };

    static constexpr int HOME_DEG = 90;
    static constexpr unsigned long MIN_PERIOD_MS = 100;

    DigiMorpher()
        : _mode(MORPH_WALK),
          _motion(MOTION_IDLE),
          _stepsRemaining(0),
          _initialSteps(0),
          _direction(1),
          _motionStartMs(0),
          _periodMs(MIN_PERIOD_MS) {
        for (int i = 0; i < CHANNEL_COUNT; ++i) _channels[i] = nullptr;
    }

    // === lifecycle ===
    bool attachChannels(IActuatorChannel* lh, IActuatorChannel* rh,
                        IActuatorChannel* lf, IActuatorChannel* rf) {
        if (lh == nullptr || rh == nullptr ||
            lf == nullptr || rf == nullptr) return false;
        _channels[LEFT_HIP]   = lh;
        _channels[RIGHT_HIP]  = rh;
        _channels[LEFT_FOOT]  = lf;
        _channels[RIGHT_FOOT] = rf;
        _bank.addChannel(lh);
        _bank.addChannel(rh);
        _bank.addChannel(lf);
        _bank.addChannel(rf);
        return true;
    }

    int channelCount() const { return _bank.count(); }
    IActuatorChannel* channelAt(int idx) const {
        if (idx < 0 || idx >= CHANNEL_COUNT) return nullptr;
        return _channels[idx];
    }

    bool init() {
        for (int i = 0; i < CHANNEL_COUNT; ++i) {
            if (_channels[i] == nullptr) return false;
        }
        for (int i = 0; i < CHANNEL_COUNT; ++i) _channels[i]->attach();
        return true;
    }
    bool initWithTrim(ITrimStore& store,
                      int pinLH, int pinRH, int pinLF, int pinRF) {
        if (!init()) return false;
        store.applyToChannel(_channels[LEFT_HIP],   pinLH);
        store.applyToChannel(_channels[RIGHT_HIP],  pinRH);
        store.applyToChannel(_channels[LEFT_FOOT],  pinLF);
        store.applyToChannel(_channels[RIGHT_FOOT], pinRF);
        return true;
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
    // Phase 3-A/B Session 156: per-channel reverse forward (transform robot
    // mounting orientation correction compile-time path). Out-of-range idx
    // silently no-ops, mirroring setChannelTrim semantics.
    void setChannelReverse(int idx, bool reverse) {
        IActuatorChannel* ch = channelAt(idx);
        if (ch != nullptr) ch->setReverse(reverse);
    }

    // === mode state (no motion; state flag only) ===
    void setMode(MorphMode mode) { _mode = mode; }
    MorphMode mode() const       { return _mode; }

    // === motion: Async setup ===
    void homeAsync(unsigned long nowMs) {
        _motion = MOTION_HOME;
        _stepsRemaining = 1;
        _initialSteps = 1;
        _motionStartMs = nowMs;
        for (int i = 0; i < CHANNEL_COUNT; ++i) _osc[i].stop();
        long targets[CHANNEL_COUNT] = {HOME_DEG, HOME_DEG, HOME_DEG, HOME_DEG};
        _bank.setTargets(targets, CHANNEL_COUNT);
    }

    // Physical transformation between walk and roll modes. Sets the
    // mode AND drives a one-cycle motion to physically reposition.
    void shiftAsync(MorphMode targetMode, int speedDegPerSec,
                    unsigned long nowMs) {
        _mode = targetMode;
        _setupMotion(MOTION_SHIFT, 1, 1, speedDegPerSec, nowMs);
    }

    // Walk-mode motions: reject (no-op) if current mode != MORPH_WALK.
    // Returns false on mode mismatch so user code can branch.
    bool walkAsync(int steps, int direction, int speedDegPerSec,
                   unsigned long nowMs) {
        if (_mode != MORPH_WALK) return false;
        _setupMotion(MOTION_WALK, steps, direction, speedDegPerSec, nowMs);
        return true;
    }
    bool turnAsync(int steps, int direction, int speedDegPerSec,
                   unsigned long nowMs) {
        if (_mode != MORPH_WALK) return false;
        _setupMotion(MOTION_TURN, steps, direction, speedDegPerSec, nowMs);
        return true;
    }

    // Roll-mode motions: reject if current mode != MORPH_ROLL.
    bool rollAsync(int cycles, int direction, int speedDegPerSec,
                   unsigned long nowMs) {
        if (_mode != MORPH_ROLL) return false;
        _setupMotion(MOTION_ROLL, cycles, direction, speedDegPerSec, nowMs);
        return true;
    }
    bool rollRotateAsync(int cycles, int direction, int speedDegPerSec,
                         unsigned long nowMs) {
        if (_mode != MORPH_ROLL) return false;
        _setupMotion(MOTION_ROLL_ROTATE, cycles, direction, speedDegPerSec, nowMs);
        return true;
    }

    // Mode-agnostic motions (work in both modes).
    void pushupAsync(int cycles, int speedDegPerSec, unsigned long nowMs) {
        _setupMotion(MOTION_PUSHUP, cycles, 1, speedDegPerSec, nowMs);
    }
    void danceAsync(int cycles, int speedDegPerSec, unsigned long nowMs) {
        _setupMotion(MOTION_DANCE, cycles, 1, speedDegPerSec, nowMs);
    }

    void stop() {
        for (int i = 0; i < CHANNEL_COUNT; ++i) _osc[i].stop();
        _motion = MOTION_IDLE;
        _stepsRemaining = 0;
        long targets[CHANNEL_COUNT] = {HOME_DEG, HOME_DEG, HOME_DEG, HOME_DEG};
        _bank.setTargets(targets, CHANNEL_COUNT);
    }

    // === motion: Blocking variants (D8) ===
    void homeBlocking()                                                 { homeAsync(_getNowMs()); _blockingPoll(); }
    void shiftBlocking(MorphMode targetMode, int speed)                 { shiftAsync(targetMode, speed, _getNowMs()); _blockingPoll(); }
    bool walkBlocking(int steps, int direction, int speed)              { bool ok = walkAsync(steps, direction, speed, _getNowMs()); if (ok) _blockingPoll(); return ok; }
    bool turnBlocking(int steps, int direction, int speed)              { bool ok = turnAsync(steps, direction, speed, _getNowMs()); if (ok) _blockingPoll(); return ok; }
    bool rollBlocking(int cycles, int direction, int speed)             { bool ok = rollAsync(cycles, direction, speed, _getNowMs()); if (ok) _blockingPoll(); return ok; }
    bool rollRotateBlocking(int cycles, int direction, int speed)       { bool ok = rollRotateAsync(cycles, direction, speed, _getNowMs()); if (ok) _blockingPoll(); return ok; }
    void pushupBlocking(int cycles, int speed)                          { pushupAsync(cycles, speed, _getNowMs()); _blockingPoll(); }
    void danceBlocking(int cycles, int speed)                           { danceAsync(cycles, speed, _getNowMs()); _blockingPoll(); }
    void waitUntilIdle()                                                { _blockingPoll(); }

    // === query ===
    bool isIdle() const           { return _motion == MOTION_IDLE; }
    MotionId currentMotion() const { return _motion; }

    void tick(unsigned long nowMs) {
        if (_motion == MOTION_IDLE) return;
        if (_motion == MOTION_HOME) {
            if (_bank.allReached()) {
                _motion = MOTION_IDLE;
                _stepsRemaining = 0;
            }
            return;
        }
        long targets[CHANNEL_COUNT];
        for (int i = 0; i < CHANNEL_COUNT; ++i) {
            targets[i] = _osc[i].valueAt(nowMs);
        }
        _bank.setTargets(targets, CHANNEL_COUNT);

        unsigned long elapsed = nowMs - _motionStartMs;
        unsigned long completedCycles =
            (_periodMs > 0) ? (elapsed / _periodMs) : 0;
        if ((int)completedCycles >= _initialSteps) {
            for (int i = 0; i < CHANNEL_COUNT; ++i) _osc[i].stop();
            long home[CHANNEL_COUNT] = {HOME_DEG, HOME_DEG, HOME_DEG, HOME_DEG};
            _bank.setTargets(home, CHANNEL_COUNT);
            _motion = MOTION_IDLE;
            _stepsRemaining = 0;
        }
    }

private:
    ChannelBank _bank;
    IActuatorChannel* _channels[CHANNEL_COUNT];
    SineOscillator _osc[CHANNEL_COUNT];
    MorphMode _mode;
    MotionId _motion;
    int _stepsRemaining;
    int _initialSteps;
    int _direction;
    unsigned long _motionStartMs;
    unsigned long _periodMs;

    struct MotionShape {
        int amp[CHANNEL_COUNT];
        double phase[CHANNEL_COUNT];
        int offset[CHANNEL_COUNT];   // delta added to HOME_DEG per channel (balance bias)
    };

    static constexpr double PI_            = 3.14159265358979;
    static constexpr double HALF_PI_       = 1.57079632679490;
    static constexpr double THREE_HALF_PI_ = 4.71238898038469;

    // ── Mirror-mount physics (channels LEFT_HIP, RIGHT_HIP, LEFT_FOOT,
    //    RIGHT_FOOT) ── same principle as DigiBiped: a left/right pair driven
    //    in-phase moves in OPPOSITE physical directions (alternating); driven
    //    anti-phase it moves the SAME physical direction (together). Phase
    //    *relationships* are derived from this physics per motion; amplitude /
    //    offset magnitudes are DigiCode-original (Phase E hardware-tunable).
    //    The earlier anti-phase-hip WALK/TURN ({0, π} hips) produced a sumo-
    //    shuffle on a mirror-mounted frame (Session 159).
    //    NOTE: roll-mode mechanics (ROLL / ROLL_ROTATE) depend on the
    //    transformer linkage and need Phase E hardware confirmation.
    static constexpr MotionShape SHIFT_SHAPE       = {{45, 45, 0, 0},   {0.0, PI_, 0.0, 0.0},               {0, 0, 0, 0}};   // hips fold together (anti-phase elec)
    static constexpr MotionShape WALK_SHAPE        = {{26, 26, 14, 14}, {0.0, 0.0, HALF_PI_, HALF_PI_},     {0, 0, 3, -3}};  // alternating legs (in-phase hips)
    static constexpr MotionShape TURN_SHAPE        = {{22, 8, 14, 14},  {0.0, 0.0, HALF_PI_, HALF_PI_},     {0, 0, 0, 0}};   // dir swaps hip amp
    static constexpr MotionShape ROLL_SHAPE        = {{0, 0, 38, 38},   {0.0, 0.0, 0.0, PI_},               {0, 0, 0, 0}};   // feet roll together (anti-phase elec)
    static constexpr MotionShape ROLL_ROTATE_SHAPE = {{5, 5, 33, 33},   {0.0, PI_, 0.0, 0.0},               {0, 0, 0, 0}};   // feet spin opposite (in-phase elec)
    static constexpr MotionShape PUSHUP_SHAPE      = {{30, 30, 18, 18}, {0.0, PI_, 0.0, PI_},               {0, 0, 0, 0}};   // push together (anti-phase elec)
    static constexpr MotionShape DANCE_SHAPE       = {{22, 18, 20, 24}, {0.0, 0.0, HALF_PI_, THREE_HALF_PI_}, {0, 0, 0, 0}}; // expressive in-phase hips

    const MotionShape& _shapeFor(MotionId m) const {
        switch (m) {
            case MOTION_SHIFT:       return SHIFT_SHAPE;
            case MOTION_WALK:        return WALK_SHAPE;
            case MOTION_TURN:        return TURN_SHAPE;
            case MOTION_ROLL:        return ROLL_SHAPE;
            case MOTION_ROLL_ROTATE: return ROLL_ROTATE_SHAPE;
            case MOTION_PUSHUP:      return PUSHUP_SHAPE;
            case MOTION_DANCE:       return DANCE_SHAPE;
            default:                 return WALK_SHAPE;
        }
    }

    void _setupMotion(MotionId m, int steps, int direction,
                      int speedDegPerSec, unsigned long nowMs) {
        _motion = m;
        _stepsRemaining = steps;
        _initialSteps = (steps > 0) ? steps : 1;
        _direction = (direction >= 0) ? 1 : -1;
        _motionStartMs = nowMs;

        const MotionShape& s = _shapeFor(m);

        // Working copy + physically-meaningful direction transform (NOT a
        // global amplitude sign flip — Session 159).
        int    amp[CHANNEL_COUNT];
        double phase[CHANNEL_COUNT];
        int    offset[CHANNEL_COUNT];
        for (int i = 0; i < CHANNEL_COUNT; ++i) {
            amp[i]    = s.amp[i];
            phase[i]  = s.phase[i];
            offset[i] = s.offset[i];
        }
        _applyDirection(m, amp, phase);

        int maxAmp = 0;
        for (int i = 0; i < CHANNEL_COUNT; ++i) {
            int a = (amp[i] >= 0) ? amp[i] : -amp[i];
            if (a > maxAmp) maxAmp = a;
        }
        if (maxAmp == 0) maxAmp = 1;
        _periodMs = (speedDegPerSec > 0)
            ? (unsigned long)(4 * maxAmp * 1000 / speedDegPerSec)
            : 1000;
        if (_periodMs < MIN_PERIOD_MS) _periodMs = MIN_PERIOD_MS;

        for (int i = 0; i < CHANNEL_COUNT; ++i) {
            _osc[i].setAmplitude(amp[i]);
            _osc[i].setOffset(HOME_DEG + offset[i]);
            _osc[i].setPeriod(_periodMs);
            _osc[i].setPhase(phase[i]);
            _osc[i].start(nowMs);
        }
    }

    // Physically-meaningful direction transform (mirror-mount aware):
    //   WALK            : forward flips the foot weight-shift phase by π
    //                     (Session 160 hardware finding; base π/2 = backward).
    //   ROLL / ROLL_ROTATE : reverse rolls/spins by flipping the foot phase;
    //                     direction convention left unchanged pending Phase E
    //                     roll-mode hardware verification (different mechanism).
    //   TURN            : right turn swaps the asymmetric hip amplitudes.
    //   SHIFT/PUSHUP/DANCE : invoked with direction = +1 (no transform).
    void _applyDirection(MotionId m, int amp[CHANNEL_COUNT],
                         double phase[CHANNEL_COUNT]) {
        switch (m) {
            case MOTION_WALK:
                // Session 160: forward flips the foot weight-shift phase by π
                // (base π/2 drives the body backward on the mirror-mounted
                // frame — same hardware finding as DigiBiped WALK).
                if (_direction > 0) {
                    phase[LEFT_FOOT]  += PI_;
                    phase[RIGHT_FOOT] += PI_;
                }
                break;
            case MOTION_ROLL:
            case MOTION_ROLL_ROTATE:
                // Roll-mode is a different mechanism (rolling, not gait) and is
                // unverified on hardware (Phase E roll-mode check). Direction
                // convention left unchanged — NOT assumed to share WALK's
                // reversal.
                if (_direction < 0) {
                    phase[LEFT_FOOT]  += PI_;
                    phase[RIGHT_FOOT] += PI_;
                }
                break;
            case MOTION_TURN:
                if (_direction < 0) {
                    int t = amp[LEFT_HIP];
                    amp[LEFT_HIP]  = amp[RIGHT_HIP];
                    amp[RIGHT_HIP] = t;
                }
                break;
            default:
                break;
        }
    }

#ifdef ARDUINO_ARCH_ESP32
    unsigned long _getNowMs() const { return millis(); }
    void _blockingPoll() {
        while (!isIdle()) {
            tick(millis());
            delay(1);
        }
    }
#else
    unsigned long _getNowMs() const { return 0; }
    void _blockingPoll() {}
#endif
};

#endif  // DIGIMORPHER_H
