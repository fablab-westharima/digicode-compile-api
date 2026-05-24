// SineOscillator host-side unit tests (Phase A-δ).
//
// Verifies the Layer 4 pure-math primitive against the formula
//   valueAt(nowMs) = offset + amplitude * sin(2π * (nowMs - startMs)
//                                              / periodMs + phaseRad)
// and the boundary contracts: !started / period 0 / phase shift / multi-
// oscillator quarter-phase sync (the 60.md §1 Phase A-δ 完了条件 "4
// channel bank で SineOscillator が phase 同期動作 (mock unit test)").
//
// 8 cases. No IActuatorChannel mock needed — SineOscillator is decoupled
// from ChannelBank by design (Session 142 case 23 incident A discipline:
// each layer independently verifiable).

#include <gtest/gtest.h>

#include <cmath>

#include "motion/SineOscillator.h"

namespace {
constexpr double TWO_PI = 6.283185307179586476925286766559;
constexpr double HALF_PI = TWO_PI / 4.0;
}  // namespace

TEST(SineOscillator, DefaultConstructionIsRestStateNotStarted) {
    SineOscillator osc;
    EXPECT_EQ(osc.getAmplitude(), 0);
    EXPECT_EQ(osc.getOffset(), 0);
    EXPECT_EQ(osc.getPeriod(), 1000u);
    EXPECT_DOUBLE_EQ(osc.getPhase(), 0.0);
    EXPECT_FALSE(osc.isStarted());
    // Pre-start always returns offset (the rest position).
    EXPECT_EQ(osc.valueAt(5000), 0);
}

TEST(SineOscillator, StartRecordsAndSetsStartedTrue) {
    SineOscillator osc;
    osc.setAmplitude(30);
    osc.setOffset(90);
    osc.setPeriod(2000);
    osc.start(1000);
    EXPECT_TRUE(osc.isStarted());

    osc.stop();
    EXPECT_FALSE(osc.isStarted());
    // After stop, valueAt drops back to offset regardless of elapsed time.
    EXPECT_EQ(osc.valueAt(1500), 90);
}

TEST(SineOscillator, ValueAtStartWithZeroPhaseEqualsOffset) {
    // At nowMs == startMs with phase 0, elapsed=0 → sin(0)=0 → value = offset.
    SineOscillator osc;
    osc.setAmplitude(30);
    osc.setOffset(90);
    osc.setPeriod(2000);
    osc.start(1000);

    EXPECT_EQ(osc.valueAt(1000), 90);
}

TEST(SineOscillator, ValueAtQuarterPeriodReachesAmplitudePeak) {
    // elapsed = period/4, phase=0 → sin(π/2)=1 → value = offset + amplitude.
    SineOscillator osc;
    osc.setAmplitude(30);
    osc.setOffset(90);
    osc.setPeriod(2000);  // quarter = 500
    osc.start(1000);

    EXPECT_EQ(osc.valueAt(1500), 120);  // 90 + 30*sin(π/2)
}

TEST(SineOscillator, ValueAtHalfPeriodReturnsOffset) {
    // elapsed = period/2, phase=0 → sin(π) ≈ 0 → value = offset.
    SineOscillator osc;
    osc.setAmplitude(30);
    osc.setOffset(90);
    osc.setPeriod(2000);  // half = 1000
    osc.start(1000);

    const long v = osc.valueAt(2000);
    EXPECT_GE(v, 89);
    EXPECT_LE(v, 91);  // truncation tolerance for sin(π) ≈ 1.22e-16
}

TEST(SineOscillator, PhaseShiftSetsInitialValueAheadInCycle) {
    // phase = π/2 → at start, sin(π/2)=1 → value = offset + amplitude.
    SineOscillator osc;
    osc.setAmplitude(30);
    osc.setOffset(90);
    osc.setPeriod(2000);
    osc.setPhase(HALF_PI);
    osc.start(1000);

    EXPECT_EQ(osc.valueAt(1000), 120);  // sin(π/2)=1

    // And at +period/4 the curve has advanced another quarter → sin(π)≈0.
    const long v = osc.valueAt(1500);
    EXPECT_GE(v, 89);
    EXPECT_LE(v, 91);
}

TEST(SineOscillator, PeriodZeroGuardReturnsOffsetInsteadOfDivByZero) {
    SineOscillator osc;
    osc.setAmplitude(30);
    osc.setOffset(90);
    osc.setPeriod(0);  // guard path
    osc.start(1000);
    EXPECT_EQ(osc.valueAt(1500), 90);
    EXPECT_EQ(osc.valueAt(50000), 90);
}

TEST(SineOscillator, FourOscillatorsAtQuarterPhaseProduceSyncedCurve) {
    // 60.md §1 Phase A-δ 完了条件: "4 channel bank で SineOscillator が
    // phase 同期動作". Four oscillators sharing one startMs with phases
    // 0 / π/2 / π / 3π/2 should produce the same values as a single
    // oscillator sampled at quarter-period offsets.
    SineOscillator base, q1, q2, q3;
    for (auto* o : {&base, &q1, &q2, &q3}) {
        o->setAmplitude(30);
        o->setOffset(90);
        o->setPeriod(4000);  // quarter = 1000 ms
    }
    base.setPhase(0.0);
    q1.setPhase(HALF_PI);
    q2.setPhase(HALF_PI * 2);
    q3.setPhase(HALF_PI * 3);

    const unsigned long t0 = 10000;
    base.start(t0);
    q1.start(t0);
    q2.start(t0);
    q3.start(t0);

    // At nowMs=t0: base sees sin(0), q1 sees sin(π/2), q2 sees sin(π),
    // q3 sees sin(3π/2).
    EXPECT_EQ(base.valueAt(t0), 90);   //  90 + 30·0
    EXPECT_EQ(q1.valueAt(t0), 120);    //  90 + 30·1
    EXPECT_NEAR(q2.valueAt(t0), 90, 1);  // truncation tolerance
    EXPECT_EQ(q3.valueAt(t0), 60);     //  90 + 30·(-1)

    // Advancing one quarter period: each oscillator advances one phase
    // step in the cycle, so values rotate (base = old q1, etc.).
    const unsigned long t1 = t0 + 1000;
    EXPECT_EQ(base.valueAt(t1), 120);
    EXPECT_NEAR(q1.valueAt(t1), 90, 1);
    EXPECT_EQ(q2.valueAt(t1), 60);
    EXPECT_NEAR(q3.valueAt(t1), 90, 1);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
