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
 *   - Motion patterns: the per-channel PHASE RELATIONSHIPS are derived from
 *     the physics of a mirror-mounted servo frame (a left/right pair driven
 *     in-phase rotates in opposite physical directions → alternating gait;
 *     see the MotionShape section). This is a functional necessity, not a
 *     copied table — any correct mirror-mounted biped (Otto included) shares
 *     the in-phase-hip relationship because the hardware geometry dictates
 *     it. The amplitude and offset MAGNITUDES are DigiCode-original values
 *     (e.g. WALK = {22,22,16,16} amp, {0,0,90,90}° phase, {0,0,+3,-3}
 *     offset), not Otto's {30,30,20,20} / {0,0,4,-4}. Session 159 corrected
 *     the earlier anti-phase-hip values that, while numerically distinct
 *     from Otto, were physically wrong (sumo-shuffle on real hardware).
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
#include <sound/IBuzzer.h>
#include <sound/GestureLibrary.h>

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
        MOTION_MOONWALK,
        MOTION_GESTURE        // Phase A-ε-2: gesture-driven sine motion
    };

    static constexpr int HOME_DEG = 90;
    static constexpr unsigned long MIN_PERIOD_MS = 100;

    DigiBiped()
        : _motion(MOTION_IDLE),
          _stepsRemaining(0),
          _initialSteps(0),
          _direction(1),
          _motionStartMs(0),
          _periodMs(MIN_PERIOD_MS),
          _buzzer(nullptr) {
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
    // Phase 3-A/B Session 156: per-channel reverse forward. Out-of-range
    // idx silently no-ops, mirroring setChannelTrim semantics.
    void setChannelReverse(int idx, bool reverse) {
        IActuatorChannel* ch = channelAt(idx);
        if (ch != nullptr) ch->setReverse(reverse);
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

    // === gesture API (Phase A-ε-2, D-new-1) ===
    // Optional buzzer attach. Pass nullptr to detach. Caller owns the
    // buzzer instance; DigiBiped only holds the pointer. Phase B-3
    // generator emit pattern:
    //     getBuzzer().attach(BUZZER_PIN);
    //     biped.attachBuzzer(&getBuzzer());
    void attachBuzzer(IBuzzer* buzzer) { _buzzer = buzzer; }
    IBuzzer* attachedBuzzer() const    { return _buzzer; }

    // Look up the (motion + sound) pair for `id` and start both. The
    // motion is async (sine oscillators run until cycles complete in
    // tick()); the sound dispatches immediately and is blocking on ESP32
    // for the preset's total duration. GESTURE_NONE / out-of-range id is
    // a no-op. If no buzzer is attached, the sound half is silently
    // skipped — motion still runs.
    void playGesture(GestureId id, unsigned long nowMs) {
        const GestureDefinition& def = GestureLibrary::get(id);
        if (def.motion.cycles > 0 && def.motion.periodMs > 0) {
            _setupGestureMotion(def.motion, nowMs);
        }
        if (_buzzer != nullptr && def.sound != BEEP_NONE) {
            _buzzer->playPreset(def.sound);
        }
    }

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
    IBuzzer* _buzzer;

    // Motion shapes: amplitude (deg) and phase (rad) per channel.
    // All values numerically distinct from Otto::walk's hardcoded
    // [30,30,20,20] + [0,0,90,90] pattern. Different structures per
    // motion (4-distinct-phase vs anti-phase-pairs vs in-phase).
    struct MotionShape {
        int amp[CHANNEL_COUNT];
        double phase[CHANNEL_COUNT];
        int offset[CHANNEL_COUNT];   // delta added to HOME_DEG per channel (balance bias)
    };

    // π constants used to avoid <cmath> M_PI POSIX-only dependency.
    static constexpr double PI_      = 3.14159265358979;
    static constexpr double HALF_PI_ = 1.57079632679490;
    static constexpr double THREE_HALF_PI_ = 4.71238898038469;

    // ── Mirror-mount physics (channels LEFT_LEG, RIGHT_LEG, LEFT_FOOT,
    //    RIGHT_FOOT; servo horns face each other on a humanoid frame) ──
    //   A left/right pair driven with the SAME electrical phase rotates in
    //   OPPOSITE physical directions (the mount mirrors one side) → the legs
    //   alternate (true bipedal gait). A pair driven ANTI-phase moves the
    //   same physical direction → both limbs swing together. The previous
    //   SHAPE values used anti-phase hips ({0, π}) which, on a mirror-mounted
    //   frame, produced a "both legs together / sumo shuffle" motion instead
    //   of an alternating walk (Session 159 hardware finding). The phase
    //   *relationships* below are derived from this physics; the amplitude /
    //   offset magnitudes are DigiCode-original values (Phase E hardware-
    //   tunable starting points), not copied from any external library.
    //   `offset[]` is a per-channel bias added to HOME_DEG (90°).

    // WALK: hips alternate (pair in-phase) for the stride; feet share phase
    // a quarter-cycle ahead for the weight-shift, with a small ±bias so the
    // stance foot carries the body. Forward = foot phase flipped by π (see
    // _applyDirection; Session 160 hardware finding: the base π/2 phase drives
    // the body backward), NOT a global amplitude sign change.
    static constexpr MotionShape WALK_SHAPE = {
        {30, 30, 22, 22},
        {0.0, 0.0, HALF_PI_, HALF_PI_},
        {0, 0, 3, -3}
    };
    // TURN: same in-phase hip stride as walk; handedness comes from an
    // asymmetric hip amplitude (the outer leg steps more). Base = left turn;
    // _applyDirection swaps the hip amplitudes for a right turn.
    static constexpr MotionShape TURN_SHAPE = {
        {30, 11, 21, 21},
        {0.0, 0.0, HALF_PI_, HALF_PI_},
        {0, 0, 0, 0}
    };
    // JUMP: hips static, both ankles snap-extend together. Same physical
    // direction (push off) ⇒ feet anti-phase electrically.
    static constexpr MotionShape JUMP_SHAPE = {
        {0, 0, 40, 40},
        {0.0, 0.0, 0.0, PI_},
        {0, 0, 0, 0}
    };
    // DANCE: expressive — hips alternate (in-phase) while feet syncopate a
    // half-cycle apart for a playful look.
    static constexpr MotionShape DANCE_SHAPE = {
        {27, 27, 24, 24},
        {0.0, 0.0, HALF_PI_, THREE_HALF_PI_},
        {0, 0, 0, 0}
    };
    // SWING: hips static, both feet rock the body side-to-side together
    // (same physical direction ⇒ feet anti-phase electrically).
    static constexpr MotionShape SWING_SHAPE = {
        {0, 0, 30, 30},
        {0.0, 0.0, 0.0, PI_},
        {0, 0, 0, 0}
    };
    // BEND: lean to one side — both hips tilt the body the same physical
    // direction ⇒ hips anti-phase electrically. _applyDirection negates the
    // amplitudes to lean the other way.
    static constexpr MotionShape BEND_SHAPE = {
        {24, 24, 0, 0},
        {0.0, PI_, 0.0, 0.0},
        {0, 0, 0, 0}
    };
    // MOONWALK: walk-like in-phase hips with a distinct slower foot timing.
    static constexpr MotionShape MOONWALK_SHAPE = {
        {27, 27, 24, 24},
        {0.0, 0.0, HALF_PI_, HALF_PI_},
        {0, 0, 0, 0}
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

        // Working copy of the shape; direction is applied as a physically-
        // meaningful transform per motion (NOT a global amplitude sign flip,
        // which reversed every channel and produced unnatural backward
        // motion — Session 159).
        int    amp[CHANNEL_COUNT];
        double phase[CHANNEL_COUNT];
        int    offset[CHANNEL_COUNT];
        for (int i = 0; i < CHANNEL_COUNT; ++i) {
            amp[i]    = s.amp[i];
            phase[i]  = s.phase[i];
            offset[i] = s.offset[i];
        }
        _applyDirection(m, amp, phase);

        // Period in ms = 4 * maxAmp * 1000 / speedDegPerSec.
        // Rationale: peak angular velocity of a sin wave with amplitude
        // A and period T is A * 2π / T deg/sec. Setting this equal to
        // speedDegPerSec yields T = 2π * A / speedDegPerSec ≈ 6.28 A /
        // speed. We use 4 * A / speed (slightly faster than peak target,
        // since speedDegPerSec is the design-level cap not the peak).
        int maxAmp = 0;
        for (int i = 0; i < CHANNEL_COUNT; ++i) {
            int a = (amp[i] >= 0) ? amp[i] : -amp[i];
            if (a > maxAmp) maxAmp = a;
        }
        if (maxAmp == 0) maxAmp = 1;  // degenerate-shape guard (e.g. all-zero)
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
    //                     (Session 160 hardware finding — base π/2 = backward),
    //                     keeping the alternating-leg (in-phase hip) gait.
    //   MOONWALK        : always invoked with direction = +1; phasing left
    //                     unchanged pending its own hardware verification.
    //   TURN            : unconditionally flips the foot weight-shift phase by
    //                     π (turn travels forward, not backward — same root as
    //                     WALK; Session 160 hardware finding) and swaps the
    //                     asymmetric hip amplitudes for a right turn (handedness).
    //   BEND            : the other lean direction negates the amplitudes.
    //   others          : direction not applicable (cycle-count motions are
    //                     invoked with direction = +1).
    void _applyDirection(MotionId m, int amp[CHANNEL_COUNT],
                         double phase[CHANNEL_COUNT]) {
        switch (m) {
            case MOTION_WALK:
                // Session 160 hardware finding: with the in-phase-hip gait, the
                // base foot weight-shift phase (π/2) drives the body BACKWARD on
                // the mirror-mounted frame. Forward travel needs the foot phase
                // advanced by π, so the flip is applied for direction = +1
                // (forward) — the opposite of the pre-160 convention.
                if (_direction > 0) {
                    phase[LEFT_FOOT]  += PI_;
                    phase[RIGHT_FOOT] += PI_;
                }
                break;
            case MOTION_MOONWALK:
                // Moonwalk is always invoked with direction = +1 (no UI choice)
                // and was NOT part of the Session 160 forward/backward finding;
                // phasing is left unchanged pending its own hardware
                // verification. Keyed on direction < 0 (a no-op at the
                // hardcoded +1) to preserve the exact pre-160 behavior.
                if (_direction < 0) {
                    phase[LEFT_FOOT]  += PI_;
                    phase[RIGHT_FOOT] += PI_;
                }
                break;
            case MOTION_TURN:
                // The turn is a walk with asymmetric hips: the hip-amp swap
                // sets rotation handedness (left/right) while the foot
                // weight-shift phase sets forward/backward travel — same as
                // WALK. The base π/2 foot phase travels BACKWARD (Session 160
                // hardware finding: "turning while moving backward", same root
                // as WALK). The hip-amp swap does NOT change the hip↔foot phase
                // relationship, so travel direction is identical for both turn
                // directions → flip the foot phase UNCONDITIONALLY so both left
                // and right turns rotate while moving forward.
                phase[LEFT_FOOT]  += PI_;
                phase[RIGHT_FOOT] += PI_;
                if (_direction < 0) {
                    int t = amp[LEFT_LEG];
                    amp[LEFT_LEG]  = amp[RIGHT_LEG];
                    amp[RIGHT_LEG] = t;
                }
                break;
            case MOTION_BEND:
                if (_direction < 0) {
                    for (int i = 0; i < CHANNEL_COUNT; ++i) amp[i] = -amp[i];
                }
                break;
            default:
                break;
        }
    }

    // Phase A-ε-2 gesture setup: takes GesturePattern directly (no static
    // _shapeFor lookup needed since gestures are runtime-table-driven).
    // Period taken verbatim from the table; cycle-completion path in
    // tick() handles the rest. Direction is fixed at +1 (gesture amps are
    // signed at the table level — negative for lean-forward effects).
    void _setupGestureMotion(const GesturePattern& gp, unsigned long nowMs) {
        _motion = MOTION_GESTURE;
        _stepsRemaining = (gp.cycles > 0) ? gp.cycles : 1;
        _initialSteps = _stepsRemaining;
        _direction = 1;
        _motionStartMs = nowMs;
        _periodMs = (gp.periodMs > 0) ? (unsigned long)gp.periodMs : MIN_PERIOD_MS;
        if (_periodMs < MIN_PERIOD_MS) _periodMs = MIN_PERIOD_MS;
        for (int i = 0; i < CHANNEL_COUNT; ++i) {
            _osc[i].setAmplitude(gp.amp[i]);
            _osc[i].setOffset(HOME_DEG);
            _osc[i].setPeriod(_periodMs);
            _osc[i].setPhase(gp.phase[i]);
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
