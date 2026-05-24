/*
 * DigiBiped - Layer 5 biped (humanoid) robot API
 * Copyright (C) 2026 DigiCo LLC
 *
 * Licensed under the GNU Affero General Public License version 3 or later.
 * See LICENSE file for full terms.
 *
 * Origin declaration: original design. Not derived from OttoDIY/OttoDIYLib
 * (GPL-3.0) or any other GPL/AGPL upstream. The full T5 12-item anti-
 * derivation discipline applies — see `plans/active/60_robotics-redesign-
 * implementation-plan.md` §4 for canonical grep patterns. Highlights for
 * this lib (none of these identifiers / signatures / structures appear
 * below):
 *   - No Otto gesture #define name set (T5 §1). Gesture support is the
 *     separate Phase A-ε-2 GestureLibrary subsystem.
 *   - No Otto helper method identifiers (T5 §3). Motion API uses common
 *     biped vocabulary (walk / turn / jump / dance / swing / bend /
 *     moonwalk) split per D8 = walk() 廃止 → walkBlocking + walkAsync
 *     with an explicit speedDegPerSec parameter (no period_ms dropdown).
 *   - No Otto multi-servo helper signatures (T5 §7 + §8). Target
 *     propagation goes through ChannelBank::setTargets(const long*, int)
 *     and motion uses N independent SineOscillator instances (one per
 *     channel) calling start(nowMs) + valueAt(nowMs); orchestration is
 *     this class's tick(nowMs).
 *   - No legacy-humanoid instance variable name (T5 §11). The instance
 *     name in generator emit is `biped`.
 *   - Motion patterns (amplitude / phase per channel per motion) are new
 *     numerical designs structurally distinct from Otto's hardcoded
 *     {30,30,20,20} amplitude + {0,0,90,90} phase. WALK_SHAPE uses
 *     {25,25,18,18} + 4-distinct quarter-phases {0, π, π/2, 3π/2}; the
 *     other 6 motions use further distinct value+structure combinations.
 *
 * Platform abstraction: header-inline implementation. Blocking polling
 * methods (walkBlocking etc.) are #ifdef ARDUINO_ARCH_ESP32 — on native
 * (host tests) they call the *Async setup but do NOT poll; tests drive
 * tick(nowMs) manually. Pure-logic state transitions and target-
 * propagation are observable from host tests via mock IActuatorChannel.
 *
 * Channel index (single source of truth for case 23 incident D label
 * reconciliation — Phase C i18n + Phase D ServoTrimDialog preset MUST
 * follow this mapping):
 *   0 = LEFT_LEG    (left hip / thigh servo)
 *   1 = RIGHT_LEG   (right hip / thigh servo)
 *   2 = LEFT_FOOT   (left ankle servo)
 *   3 = RIGHT_FOOT  (right ankle servo)
 *
 * Lifecycle (D8 + Session 143 lifecycle case c "両方併用、後勝ち"):
 *   initWithTrim(store, pins): boot-time push — store.applyToChannel for
 *     each (channel, pin) pair after attach(). Lifecycle case a.
 *   setChannelTrim(idx, deg): runtime override forwarded to channel→
 *     setTrim. Caller drives via ServoTrimDialog (Phase D transport).
 *     Lifecycle case b. Precedence = call-sequence's responsibility
 *     (last write wins).
 */

#ifndef DIGIBIPED_H
#define DIGIBIPED_H

#include <actuator/IActuatorChannel.h>
#include <motion/ChannelBank.h>
#include <motion/SineOscillator.h>
#include <trim/ITrimStore.h>

#ifdef ARDUINO_ARCH_ESP32
  #include <Arduino.h>
#endif

class DigiBiped {
public:
    enum ChannelIndex {
        LEFT_LEG     = 0,
        RIGHT_LEG    = 1,
        LEFT_FOOT    = 2,
        RIGHT_FOOT   = 3,
        CHANNEL_COUNT = 4
    };

    enum MotionId {
        MOTION_IDLE     = 0,
        MOTION_HOME,
        MOTION_WALK,
        MOTION_TURN,
        MOTION_JUMP,
        MOTION_DANCE,
        MOTION_SWING,
        MOTION_BEND,
        MOTION_MOONWALK
    };

    static constexpr int HOME_DEG = 90;
    static constexpr unsigned long MIN_PERIOD_MS = 100;

    DigiBiped()
        : _motion(MOTION_IDLE),
          _stepsRemaining(0),
          _initialSteps(0),
          _direction(1),
          _motionStartMs(0),
          _periodMs(MIN_PERIOD_MS) {
        for (int i = 0; i < CHANNEL_COUNT; ++i) _channels[i] = nullptr;
    }

    // === lifecycle ===
    // Register the 4 channels with the internal bank. nullptr rejected.
    // Channel lifetime is the caller's responsibility (bank does not own).
    bool attachChannels(IActuatorChannel* ll, IActuatorChannel* rl,
                        IActuatorChannel* lf, IActuatorChannel* rf) {
        if (ll == nullptr || rl == nullptr ||
            lf == nullptr || rf == nullptr) return false;
        _channels[LEFT_LEG]   = ll;
        _channels[RIGHT_LEG]  = rl;
        _channels[LEFT_FOOT]  = lf;
        _channels[RIGHT_FOOT] = rf;
        _bank.addChannel(ll);
        _bank.addChannel(rl);
        _bank.addChannel(lf);
        _bank.addChannel(rf);
        return true;
    }

    int channelCount() const { return _bank.count(); }
    IActuatorChannel* channelAt(int idx) const {
        if (idx < 0 || idx >= CHANNEL_COUNT) return nullptr;
        return _channels[idx];
    }

    // attach() all 4 channels. Returns false if any channel slot is
    // still nullptr (attachChannels was not called or it failed).
    bool init() {
        for (int i = 0; i < CHANNEL_COUNT; ++i) {
            if (_channels[i] == nullptr) return false;
        }
        for (int i = 0; i < CHANNEL_COUNT; ++i) {
            _channels[i]->attach();
        }
        return true;
    }

    // init() + per-pin trim pull from store (Session 143 lifecycle c,
    // boot-time push, case a). Runtime overrides via setChannelTrim
    // (case b).
    bool initWithTrim(ITrimStore& store,
                      int pinLL, int pinRL, int pinLF, int pinRF) {
        if (!init()) return false;
        store.applyToChannel(_channels[LEFT_LEG],   pinLL);
        store.applyToChannel(_channels[RIGHT_LEG],  pinRL);
        store.applyToChannel(_channels[LEFT_FOOT],  pinLF);
        store.applyToChannel(_channels[RIGHT_FOOT], pinRF);
        return true;
    }

    // === per-channel 3-axis settings (E1 case 23 incident A defense) ===
    // Forward to the indexed channel's IActuatorChannel::set* method.
    // Out-of-range idx silently no-ops.
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

    // === motion: Async setup ===
    // All channels return to HOME_DEG (90°). One-shot; isIdle returns
    // true once bank.allReached() (or immediately on host where mock
    // channels mark reached=true after setTarget).
    void homeAsync(unsigned long nowMs) {
        _motion = MOTION_HOME;
        _stepsRemaining = 1;
        _initialSteps = 1;
        _motionStartMs = nowMs;
        for (int i = 0; i < CHANNEL_COUNT; ++i) _osc[i].stop();
        long targets[CHANNEL_COUNT] = {HOME_DEG, HOME_DEG, HOME_DEG, HOME_DEG};
        _bank.setTargets(targets, CHANNEL_COUNT);
    }

    void walkAsync(int steps, int direction, int speedDegPerSec,
                   unsigned long nowMs) {
        _setupMotion(MOTION_WALK, steps, direction, speedDegPerSec, nowMs);
    }
    void turnAsync(int steps, int direction, int speedDegPerSec,
                   unsigned long nowMs) {
        _setupMotion(MOTION_TURN, steps, direction, speedDegPerSec, nowMs);
    }
    void jumpAsync(int speedDegPerSec, unsigned long nowMs) {
        _setupMotion(MOTION_JUMP, 1, 1, speedDegPerSec, nowMs);
    }
    void danceAsync(int cycles, int speedDegPerSec, unsigned long nowMs) {
        _setupMotion(MOTION_DANCE, cycles, 1, speedDegPerSec, nowMs);
    }
    void swingAsync(int cycles, int speedDegPerSec, unsigned long nowMs) {
        _setupMotion(MOTION_SWING, cycles, 1, speedDegPerSec, nowMs);
    }
    void bendAsync(int direction, int speedDegPerSec, unsigned long nowMs) {
        _setupMotion(MOTION_BEND, 1, direction, speedDegPerSec, nowMs);
    }
    void moonwalkAsync(int cycles, int speedDegPerSec, unsigned long nowMs) {
        _setupMotion(MOTION_MOONWALK, cycles, 1, speedDegPerSec, nowMs);
    }

    // Halt motion + return to home immediately. Discards remaining cycles.
    void stop() {
        for (int i = 0; i < CHANNEL_COUNT; ++i) _osc[i].stop();
        _motion = MOTION_IDLE;
        _stepsRemaining = 0;
        long targets[CHANNEL_COUNT] = {HOME_DEG, HOME_DEG, HOME_DEG, HOME_DEG};
        _bank.setTargets(targets, CHANNEL_COUNT);
    }

    // === motion: Blocking variants (D8) ===
    // ESP32: poll tick(millis()) + delay(1) until isIdle().
    // Native (host test): call *Async setup at nowMs=0 but do NOT poll —
    //   tests drive tick(nowMs) directly.
    void homeBlocking()                                       { homeAsync(_getNowMs());        _blockingPoll(); }
    void walkBlocking(int steps, int direction, int speed)    { walkAsync(steps, direction, speed, _getNowMs()); _blockingPoll(); }
    void turnBlocking(int steps, int direction, int speed)    { turnAsync(steps, direction, speed, _getNowMs()); _blockingPoll(); }
    void jumpBlocking(int speed)                              { jumpAsync(speed, _getNowMs()); _blockingPoll(); }
    void danceBlocking(int cycles, int speed)                 { danceAsync(cycles, speed, _getNowMs()); _blockingPoll(); }
    void swingBlocking(int cycles, int speed)                 { swingAsync(cycles, speed, _getNowMs()); _blockingPoll(); }
    void bendBlocking(int direction, int speed)               { bendAsync(direction, speed, _getNowMs()); _blockingPoll(); }
    void moonwalkBlocking(int cycles, int speed)              { moonwalkAsync(cycles, speed, _getNowMs()); _blockingPoll(); }
    void waitUntilIdle()                                      { _blockingPoll(); }

    // === query ===
    bool isIdle() const           { return _motion == MOTION_IDLE; }
    MotionId currentMotion() const { return _motion; }

    // === tick: advance async motion ===
    // Caller (background pump on ESP32, test driver on native) invokes
    // periodically with current millis() / mock clock. Idempotent:
    // multiple calls at the same nowMs produce the same targets.
    void tick(unsigned long nowMs) {
        if (_motion == MOTION_IDLE) return;
        if (_motion == MOTION_HOME) {
            // Home is a one-shot target publish. Mock channels may flag
            // reached=true immediately; ESP32 channels rely on pump()
            // ramping toward HOME_DEG. Either way, once allReached the
            // bank reports completion and we return to IDLE.
            if (_bank.allReached()) {
                _motion = MOTION_IDLE;
                _stepsRemaining = 0;
            }
            return;
        }

        // Oscillator-driven motion: compute valueAt for each channel,
        // batch-publish via ChannelBank.
        long targets[CHANNEL_COUNT];
        for (int i = 0; i < CHANNEL_COUNT; ++i) {
            targets[i] = _osc[i].valueAt(nowMs);
        }
        _bank.setTargets(targets, CHANNEL_COUNT);

        // Cycle completion check. _periodMs is one full sine cycle;
        // _initialSteps is the target cycle count.
        unsigned long elapsed = nowMs - _motionStartMs;
        unsigned long completedCycles =
            (_periodMs > 0) ? (elapsed / _periodMs) : 0;
        if ((int)completedCycles >= _initialSteps) {
            // Done. Stop oscillators, return to home, transition to IDLE.
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
    MotionId _motion;
    int _stepsRemaining;
    int _initialSteps;
    int _direction;
    unsigned long _motionStartMs;
    unsigned long _periodMs;

    // Motion shapes: amplitude (deg) and phase (rad) per channel.
    // All values numerically distinct from Otto::walk's hardcoded
    // [30,30,20,20] + [0,0,90,90] pattern. Different structures per
    // motion (4-distinct-phase vs anti-phase-pairs vs in-phase).
    struct MotionShape {
        int amp[CHANNEL_COUNT];
        double phase[CHANNEL_COUNT];
    };

    // π constants used to avoid <cmath> M_PI POSIX-only dependency.
    static constexpr double PI_      = 3.14159265358979;
    static constexpr double HALF_PI_ = 1.57079632679490;
    static constexpr double THREE_HALF_PI_ = 4.71238898038469;
    static constexpr double QUARTER_PI_ = 0.78539816339745;
    static constexpr double FIVE_QUARTER_PI_ = 3.92699081698724;
    static constexpr double TWO_THIRD_PI_ = 2.09439510239319;
    static constexpr double FOUR_THIRD_PI_ = 4.18879020478639;
    static constexpr double ONE_THIRD_PI_ = 1.04719755119660;

    // WALK: legs > feet, 4-distinct-phase quarter-rotation. Structurally
    // different from Otto's pair-aligned [0,0,90,90].
    static constexpr MotionShape WALK_SHAPE = {
        {25, 25, 18, 18},
        {0.0, PI_, HALF_PI_, THREE_HALF_PI_}
    };
    // TURN: feet > legs (counter to walk), anti-phase pairs.
    static constexpr MotionShape TURN_SHAPE = {
        {20, 20, 22, 22},
        {0.0, PI_, PI_, 0.0}
    };
    // JUMP: all in-phase, max amp.
    static constexpr MotionShape JUMP_SHAPE = {
        {35, 35, 35, 35},
        {0.0, 0.0, 0.0, 0.0}
    };
    // DANCE: asymmetric amps + 3-phase rotation.
    static constexpr MotionShape DANCE_SHAPE = {
        {28, 22, 15, 19},
        {0.0, TWO_THIRD_PI_, FOUR_THIRD_PI_, ONE_THIRD_PI_}
    };
    // SWING: legs static, feet anti-phase.
    static constexpr MotionShape SWING_SHAPE = {
        {0, 0, 25, 25},
        {0.0, 0.0, 0.0, PI_}
    };
    // BEND: legs only, anti-phase.
    static constexpr MotionShape BEND_SHAPE = {
        {20, 20, 0, 0},
        {0.0, PI_, 0.0, 0.0}
    };
    // MOONWALK: all-amp, staggered π/4 phase increments.
    static constexpr MotionShape MOONWALK_SHAPE = {
        {22, 22, 22, 22},
        {0.0, QUARTER_PI_, PI_, FIVE_QUARTER_PI_}
    };

    const MotionShape& _shapeFor(MotionId m) const {
        switch (m) {
            case MOTION_WALK:     return WALK_SHAPE;
            case MOTION_TURN:     return TURN_SHAPE;
            case MOTION_JUMP:     return JUMP_SHAPE;
            case MOTION_DANCE:    return DANCE_SHAPE;
            case MOTION_SWING:    return SWING_SHAPE;
            case MOTION_BEND:     return BEND_SHAPE;
            case MOTION_MOONWALK: return MOONWALK_SHAPE;
            default:              return WALK_SHAPE;  // unreachable for IDLE/HOME
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

        // Period in ms = 4 * maxAmp * 1000 / speedDegPerSec.
        // Rationale: peak angular velocity of a sin wave with amplitude
        // A and period T is A * 2π / T deg/sec. Setting this equal to
        // speedDegPerSec yields T = 2π * A / speedDegPerSec ≈ 6.28 A /
        // speed. We use 4 * A / speed (slightly faster than peak target,
        // since speedDegPerSec is the design-level cap not the peak).
        int maxAmp = 0;
        for (int i = 0; i < CHANNEL_COUNT; ++i) {
            if (s.amp[i] > maxAmp) maxAmp = s.amp[i];
        }
        if (maxAmp == 0) maxAmp = 1;  // degenerate-shape guard (e.g. all-zero)
        _periodMs = (speedDegPerSec > 0)
            ? (unsigned long)(4 * maxAmp * 1000 / speedDegPerSec)
            : 1000;
        if (_periodMs < MIN_PERIOD_MS) _periodMs = MIN_PERIOD_MS;

        for (int i = 0; i < CHANNEL_COUNT; ++i) {
            _osc[i].setAmplitude(s.amp[i] * _direction);
            _osc[i].setOffset(HOME_DEG);
            _osc[i].setPeriod(_periodMs);
            _osc[i].setPhase(s.phase[i]);
            _osc[i].start(nowMs);
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
    void _blockingPoll() {
        // On native, blocking is a no-op so host tests stay deterministic.
        // Tests drive tick(nowMs) manually after the *Async setup.
    }
#endif
};

#endif  // DIGIBIPED_H
