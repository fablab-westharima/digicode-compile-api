// LinearInterpolator host-side unit tests (Phase A-δ).
//
// Verifies the Layer 4 pure-math primitive: lerp accuracy at start /
// midpoint / end / past-end, isDone() boundary, and the 案 B design
// contract that rate-cap is NOT enforced inside the interpolator (the
// Layer 2 IActuatorChannel::setMaxRate() responsibility is independent).
//
// 7 cases. No IActuatorChannel mock — LinearInterpolator is pure long-
// integer arithmetic with no channel coupling.

#include <gtest/gtest.h>

#include "motion/LinearInterpolator.h"

TEST(LinearInterpolator, DefaultConstructionIsNotStartedAndIsDoneTrue) {
    LinearInterpolator lerp;
    EXPECT_FALSE(lerp.isStarted());
    EXPECT_TRUE(lerp.isDone(0));        // nothing scheduled = nothing pending
    EXPECT_TRUE(lerp.isDone(1000000));
    EXPECT_EQ(lerp.getStartValue(), 0);
    EXPECT_EQ(lerp.getEndValue(), 0);
    EXPECT_EQ(lerp.getDuration(), 0u);
    // Pre-start valueAt returns endValue (the resting target), which
    // defaults to 0 here.
    EXPECT_EQ(lerp.valueAt(500), 0);
}

TEST(LinearInterpolator, StartRecordsAndIsStartedTrue) {
    LinearInterpolator lerp;
    lerp.start(1000, 30, 90, 2000);
    EXPECT_TRUE(lerp.isStarted());
    EXPECT_EQ(lerp.getStartValue(), 30);
    EXPECT_EQ(lerp.getEndValue(), 90);
    EXPECT_EQ(lerp.getDuration(), 2000u);
    EXPECT_FALSE(lerp.isDone(1000));   // just started, full duration remains
}

TEST(LinearInterpolator, ValueAtStartReturnsStartValue) {
    LinearInterpolator lerp;
    lerp.start(1000, 30, 90, 2000);
    EXPECT_EQ(lerp.valueAt(1000), 30);
    // And before startMs (callable defensively) also returns startValue.
    EXPECT_EQ(lerp.valueAt(500), 30);
}

TEST(LinearInterpolator, ValueAtMidpointReturnsLerpedValue) {
    LinearInterpolator lerp;
    lerp.start(1000, 30, 90, 2000);
    // Midpoint: elapsed=1000 of 2000, value = 30 + (90-30) * 1000/2000 = 60.
    EXPECT_EQ(lerp.valueAt(2000), 60);
    EXPECT_FALSE(lerp.isDone(2000));

    // Quarter and three-quarter points.
    EXPECT_EQ(lerp.valueAt(1500), 45);  // 30 + 60*500/2000 = 45
    EXPECT_EQ(lerp.valueAt(2500), 75);  // 30 + 60*1500/2000 = 75
}

TEST(LinearInterpolator, ValueAtEndReturnsEndValueAndIsDoneTrue) {
    LinearInterpolator lerp;
    lerp.start(1000, 30, 90, 2000);
    EXPECT_EQ(lerp.valueAt(3000), 90);  // elapsed == duration
    EXPECT_TRUE(lerp.isDone(3000));
}

TEST(LinearInterpolator, ValueClampedPastEndAndDescendingTrajectory) {
    LinearInterpolator lerp;
    lerp.start(1000, 30, 90, 2000);
    // Past the end: stays at endValue.
    EXPECT_EQ(lerp.valueAt(5000), 90);
    EXPECT_EQ(lerp.valueAt(1000000), 90);
    EXPECT_TRUE(lerp.isDone(5000));

    // Descending trajectory (end < start) lerps correctly.
    LinearInterpolator down;
    down.start(0, 100, 20, 1000);
    EXPECT_EQ(down.valueAt(0), 100);
    EXPECT_EQ(down.valueAt(500), 60);   // 100 + (20-100)*500/1000 = 60
    EXPECT_EQ(down.valueAt(1000), 20);
    EXPECT_TRUE(down.isDone(1000));
}

TEST(LinearInterpolator, RateCapIsLayer2ResponsibilityNotEnforcedHere) {
    // Session 142 design judgment (案 B): rate-cap belongs in Layer 2
    // IActuatorChannel::setMaxRate(). LinearInterpolator publishes the
    // raw trajectory dictated by durationMs alone. Two interpolators
    // with the same start/end and the same durationMs MUST produce
    // identical valueAt(nowMs) regardless of any per-channel rate cap
    // the caller may set independently downstream. This test pins that
    // contract so future refactors don't accidentally fold a max-rate
    // clamp into Layer 4 (case 23 incident A "dual-control" trap).
    LinearInterpolator a, b;
    a.start(0, 0, 1000, 100);  // would imply 10 unit/ms if a rate cap existed
    b.start(0, 0, 1000, 100);  // identical schedule, no downstream coupling

    // The interpolator output depends only on duration, not on any
    // notion of channel-side rate cap. Sample several points to verify
    // both instances agree end-to-end.
    for (unsigned long t : {0ul, 25ul, 50ul, 75ul, 100ul, 200ul}) {
        EXPECT_EQ(a.valueAt(t), b.valueAt(t)) << "t=" << t;
    }
    // And the midpoint follows the raw lerp, not any clamp.
    EXPECT_EQ(a.valueAt(50), 500);     // 0 + 1000*50/100 = 500
    EXPECT_TRUE(a.isDone(100));
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
