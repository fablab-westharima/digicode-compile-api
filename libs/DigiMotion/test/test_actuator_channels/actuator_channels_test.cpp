// Layer 2 IActuatorChannel host-side unit tests (Phase A-γ commit 2).
//
// Covers the 6 concrete channels: ServoChannel180 / 270 / Continuous /
// StepperPoll / StepperHw / DcMotor. ESP32 HW members + calls are
// compiled out on the native env (no Arduino.h / ESP32Servo / AccelStepper
// / FastAccelStepper / LEDC), so these tests exercise the
// platform-agnostic 3-axis state, target clamp, rate cap, pump scheduling,
// and trim-aware _writeHw observability (via getLastWrittenHw()).
//
// 32 cases total. Folder name deviates from 60.md §1 commit 2 verbatim
// ("test_servo_channel/") because it now covers stepper + dc motor too.
// The deviation is mechanical-rename-only; coverage is the same.

#include <gtest/gtest.h>

#include "actuator/ServoChannel180.h"
#include "actuator/ServoChannel270.h"
#include "actuator/ContinuousServoChannel.h"
#include "actuator/StepperPollChannel.h"
#include "actuator/StepperHwChannel.h"
#include "actuator/DcMotorChannel.h"

// ============================================================================
// ServoChannel180
// ============================================================================

TEST(ServoChannel180, DefaultsAndTargetClampToZeroToOneEighty) {
    ServoChannel180 ch(27);
    EXPECT_EQ(ch.getPulseMin(), ServoChannel180::DEFAULT_PULSE_MIN_US);
    EXPECT_EQ(ch.getPulseMax(), ServoChannel180::DEFAULT_PULSE_MAX_US);
    EXPECT_EQ(ch.getMaxRate(), 0);
    EXPECT_EQ(ch.getTrim(), 0);
    EXPECT_EQ(ch.getTarget(), 90);
    EXPECT_FALSE(ch.isAttached());

    ch.setTarget(-50);
    EXPECT_EQ(ch.getTarget(), 0);
    ch.setTarget(500);
    EXPECT_EQ(ch.getTarget(), 180);
    ch.setTarget(135);
    EXPECT_EQ(ch.getTarget(), 135);
}

TEST(ServoChannel180, PulseRangeStored) {
    ServoChannel180 ch(27);
    ch.setPulseRange(500, 2500);
    EXPECT_EQ(ch.getPulseMin(), 500);
    EXPECT_EQ(ch.getPulseMax(), 2500);
}

TEST(ServoChannel180, TrimClampedAndAppliedInWriteHw) {
    ServoChannel180 ch(27);
    ch.setTrim(50);
    EXPECT_EQ(ch.getTrim(), ServoChannel180::TRIM_MAX);
    ch.setTrim(-50);
    EXPECT_EQ(ch.getTrim(), ServoChannel180::TRIM_MIN);
    ch.setTrim(5);
    EXPECT_EQ(ch.getTrim(), 5);

    ch.attach();
    ch.setTarget(100);
    ch.pump(0);  // maxRate=0 → instant jump
    EXPECT_EQ(ch.getCurrent(), 100);
    EXPECT_EQ(ch.getLastWrittenHw(), 105);  // 100 + trim 5
}

TEST(ServoChannel180, UnlimitedRateJumpsInOnePump) {
    ServoChannel180 ch(27);
    ch.attach();
    ch.setTarget(170);
    EXPECT_TRUE(ch.isActive());
    ch.pump(0);
    EXPECT_EQ(ch.getCurrent(), 170);
    EXPECT_FALSE(ch.isActive());
    EXPECT_TRUE(ch.hasReachedTarget());
}

TEST(ServoChannel180, RateCappedAdvanceOneDegreePerStepMs) {
    ServoChannel180 ch(27);
    ch.attach();
    ch.setMaxRate(100);   // 100 deg/sec → 10 ms per degree
    ch.setTarget(95);
    EXPECT_EQ(ch.getCurrent(), 90);

    // First pump advances (elapsed since _lastStepMs=0 is large).
    ch.pump(100);
    EXPECT_EQ(ch.getCurrent(), 91);
    EXPECT_EQ(ch.getLastWrittenHw(), 91);
    // Too soon for next step (105 - 100 = 5 < 10).
    ch.pump(105);
    EXPECT_EQ(ch.getCurrent(), 91);
    // Next 10 ms elapsed → advance.
    ch.pump(110);
    EXPECT_EQ(ch.getCurrent(), 92);
    ch.pump(120);
    EXPECT_EQ(ch.getCurrent(), 93);
    ch.pump(130);
    EXPECT_EQ(ch.getCurrent(), 94);
    ch.pump(140);
    EXPECT_EQ(ch.getCurrent(), 95);
    EXPECT_FALSE(ch.isActive());
}

TEST(ServoChannel180, PumpSkippedWhenDetached) {
    ServoChannel180 ch(27);
    // not attached
    ch.setTarget(170);
    ch.pump(0);
    EXPECT_EQ(ch.getCurrent(), 90);  // unchanged
    EXPECT_FALSE(ch.isActive());
}

// ============================================================================
// ServoChannel270
// ============================================================================

TEST(ServoChannel270, MidRangeDefaultAndZeroToTwoSeventyClamp) {
    ServoChannel270 ch(14);
    EXPECT_EQ(ch.getTarget(), 135);
    ch.setTarget(-10);
    EXPECT_EQ(ch.getTarget(), 0);
    ch.setTarget(400);
    EXPECT_EQ(ch.getTarget(), 270);
    ch.setTarget(200);
    EXPECT_EQ(ch.getTarget(), 200);
}

TEST(ServoChannel270, TrimAppliedThroughWriteHw) {
    ServoChannel270 ch(14);
    ch.attach();
    ch.setTrim(-7);
    ch.setTarget(200);
    ch.pump(0);
    EXPECT_EQ(ch.getCurrent(), 200);
    EXPECT_EQ(ch.getLastWrittenHw(), 193);  // 200 + (-7)
}

TEST(ServoChannel270, RateCapStepsToward) {
    ServoChannel270 ch(14);
    ch.attach();
    ch.setMaxRate(200);   // 5 ms per degree
    ch.setTarget(140);    // start 135
    ch.pump(100);
    EXPECT_EQ(ch.getCurrent(), 136);
    ch.pump(105);
    EXPECT_EQ(ch.getCurrent(), 137);
    ch.pump(110);
    EXPECT_EQ(ch.getCurrent(), 138);
}

TEST(ServoChannel270, PulseRangeWiderDefault) {
    ServoChannel270 ch(14);
    EXPECT_EQ(ch.getPulseMin(), 500);   // ASMC-04B class
    EXPECT_EQ(ch.getPulseMax(), 2500);
}

// ============================================================================
// ContinuousServoChannel
// ============================================================================

TEST(ContinuousServo, TargetClampMinusOneHundredToPlusOneHundred) {
    ContinuousServoChannel ch(15);
    EXPECT_EQ(ch.getTarget(), 0);
    ch.setTarget(-150);
    EXPECT_EQ(ch.getTarget(), -100);
    ch.setTarget(150);
    EXPECT_EQ(ch.getTarget(), 100);
    ch.setTarget(42);
    EXPECT_EQ(ch.getTarget(), 42);
}

TEST(ContinuousServo, ZeroVelocityMapsToStopCenter) {
    ContinuousServoChannel ch(15);
    ch.attach();
    ch.setTarget(0);
    ch.pump(0);
    EXPECT_EQ(ch.getLastWrittenHw(), 90);
}

TEST(ContinuousServo, FullForwardMapsToOneEighty) {
    ContinuousServoChannel ch(15);
    ch.attach();
    ch.setTarget(100);
    ch.pump(0);
    EXPECT_EQ(ch.getLastWrittenHw(), 180);
}

TEST(ContinuousServo, FullReverseMapsToZero) {
    ContinuousServoChannel ch(15);
    ch.attach();
    ch.setTarget(-100);
    ch.pump(0);
    EXPECT_EQ(ch.getLastWrittenHw(), 0);
}

TEST(ContinuousServo, TrimShiftsStopCenter) {
    ContinuousServoChannel ch(15);
    ch.attach();
    ch.setTrim(5);
    ch.setTarget(0);
    ch.pump(0);
    EXPECT_EQ(ch.getLastWrittenHw(), 95);  // 90 + trim 5
}

TEST(ContinuousServo, RateCappedAcceleration) {
    ContinuousServoChannel ch(15);
    ch.attach();
    ch.setMaxRate(50);   // 50 %/sec → 20 ms per %
    ch.setTarget(3);
    ch.pump(100);
    EXPECT_EQ(ch.getCurrent(), 1);
    ch.pump(120);
    EXPECT_EQ(ch.getCurrent(), 2);
    ch.pump(140);
    EXPECT_EQ(ch.getCurrent(), 3);
    EXPECT_FALSE(ch.isActive());
}

// ============================================================================
// StepperPollChannel
// ============================================================================

TEST(StepperPoll, DriverModeConstructorAndAttach) {
    StepperPollChannel ch(/*step*/26, /*dir*/25);
    EXPECT_FALSE(ch.isAttached());
    EXPECT_EQ(ch.getTarget(), 0);
    EXPECT_TRUE(ch.attach());
    EXPECT_TRUE(ch.isAttached());
}

TEST(StepperPoll, FourWireConstructorAndDetach) {
    StepperPollChannel ch(/*in1*/14, /*in2*/27, /*in3*/26, /*in4*/25);
    EXPECT_TRUE(ch.attach());
    ch.detach();
    EXPECT_FALSE(ch.isAttached());
}

TEST(StepperPoll, TargetAndTrimReflectedInLastWrittenHwOnPump) {
    StepperPollChannel ch(26, 25);
    ch.attach();
    ch.setTrim(3);
    ch.setTarget(200);
    ch.pump(0);
    EXPECT_EQ(ch.getLastWrittenHw(), 203);  // target + trim
}

TEST(StepperPoll, MaxRateStored) {
    StepperPollChannel ch(26, 25);
    ch.setMaxRate(800);
    EXPECT_EQ(ch.getMaxRate(), 800);
}

TEST(StepperPoll, PulseRangeNoOpReturnsZeros) {
    StepperPollChannel ch(26, 25);
    ch.setPulseRange(500, 2500);
    EXPECT_EQ(ch.getPulseMin(), 0);
    EXPECT_EQ(ch.getPulseMax(), 0);
}

TEST(StepperPoll, IsActiveAfterTargetSetUntilReached) {
    StepperPollChannel ch(26, 25);
    ch.attach();
    ch.setTarget(50);
    EXPECT_TRUE(ch.isActive());
    ch.pump(0);  // host: current jumps to target
    EXPECT_FALSE(ch.isActive());
    EXPECT_TRUE(ch.hasReachedTarget());
}

// ============================================================================
// StepperHwChannel
// ============================================================================

TEST(StepperHw, ConstructorWithEnablePinAndAttach) {
    StepperHwChannel ch(/*step*/26, /*dir*/25, /*en*/33);
    EXPECT_FALSE(ch.isAttached());
    EXPECT_TRUE(ch.attach());
    EXPECT_TRUE(ch.isAttached());
}

TEST(StepperHw, ConstructorWithoutEnablePin) {
    StepperHwChannel ch(26, 25);
    EXPECT_TRUE(ch.attach());
    ch.detach();
    EXPECT_FALSE(ch.isAttached());
}

TEST(StepperHw, TargetAndTrimReflectedInLastWrittenHwOnPump) {
    StepperHwChannel ch(26, 25);
    ch.attach();
    ch.setTrim(-4);
    ch.setTarget(1000);
    ch.pump(0);
    EXPECT_EQ(ch.getLastWrittenHw(), 996);  // target + trim
}

TEST(StepperHw, MaxRateStoredAndPulseRangeNoOp) {
    StepperHwChannel ch(26, 25);
    ch.setMaxRate(50000);
    EXPECT_EQ(ch.getMaxRate(), 50000);
    ch.setPulseRange(500, 2500);
    EXPECT_EQ(ch.getPulseMin(), 0);
    EXPECT_EQ(ch.getPulseMax(), 0);
}

// ============================================================================
// DcMotorChannel
// ============================================================================

TEST(DcMotor, DefaultsAndVelocityClamp) {
    DcMotorChannel ch(/*fwd*/16, /*rev*/17);
    EXPECT_EQ(ch.getTarget(), 0);
    EXPECT_FALSE(ch.isAttached());
    ch.setTarget(-150);
    EXPECT_EQ(ch.getTarget(), -100);
    ch.setTarget(150);
    EXPECT_EQ(ch.getTarget(), 100);
}

TEST(DcMotor, TrimClampZeroToThirty) {
    DcMotorChannel ch(16, 17);
    ch.setTrim(-5);
    EXPECT_EQ(ch.getTrim(), 0);
    ch.setTrim(50);
    EXPECT_EQ(ch.getTrim(), 30);
    ch.setTrim(10);
    EXPECT_EQ(ch.getTrim(), 10);
}

TEST(DcMotor, ForwardDirectionPositiveLastWritten) {
    DcMotorChannel ch(16, 17);
    ch.attach();
    ch.setTarget(50);
    ch.pump(0);
    EXPECT_EQ(ch.getCurrent(), 50);
    // duty = 50 * 255 / 100 = 127, positive sign = forward
    EXPECT_EQ(ch.getLastWrittenHw(), 127);
}

TEST(DcMotor, ReverseDirectionNegativeLastWritten) {
    DcMotorChannel ch(16, 17);
    ch.attach();
    ch.setTarget(-40);
    ch.pump(0);
    // |v|=40 → duty=40*255/100=102; reverse → negative sign
    EXPECT_EQ(ch.getLastWrittenHw(), -102);
}

TEST(DcMotor, TrimBoostsMagnitudeWithDeadband) {
    DcMotorChannel ch(16, 17);
    ch.attach();
    ch.setTrim(20);
    ch.setTarget(10);
    ch.pump(0);
    // |v|=10 + trim 20 = 30 → duty = 30*255/100 = 76, forward
    EXPECT_EQ(ch.getLastWrittenHw(), 76);
}

TEST(DcMotor, ZeroVelocityWritesZeroDutyBothSides) {
    DcMotorChannel ch(16, 17);
    ch.attach();
    ch.setTarget(0);
    ch.pump(0);
    EXPECT_EQ(ch.getLastWrittenHw(), 0);
}

TEST(DcMotor, TrimBoostCapsAtFullDuty) {
    DcMotorChannel ch(16, 17);
    ch.attach();
    ch.setTrim(30);
    ch.setTarget(90);
    ch.pump(0);
    // |v|=90 + trim 30 = 120 → cap to 100 → duty 255
    EXPECT_EQ(ch.getLastWrittenHw(), 255);
}

// ============================================================================
// Cross-cutting integration: rule 18 §Discipline 5 same-domain coverage spot
// (3-axis settings are uniformly observable across all 6 channel types)
// ============================================================================

TEST(AllChannels, ThreeAxisGettersRespondUniformly) {
    ServoChannel180 s180(27);
    ServoChannel270 s270(14);
    ContinuousServoChannel cs(15);
    StepperPollChannel sp(26, 25);
    StepperHwChannel sh(26, 25);
    DcMotorChannel dc(16, 17);

    IActuatorChannel* channels[] = { &s180, &s270, &cs, &sp, &sh, &dc };
    for (auto* ch : channels) {
        ch->setMaxRate(123);
        EXPECT_EQ(ch->getMaxRate(), 123);
    }

    // Trim semantics differ per channel (deg / % offset / step / deadband),
    // but the IF responds uniformly: setTrim then getTrim.
    s180.setTrim(7);   EXPECT_EQ(s180.getTrim(), 7);
    s270.setTrim(-3);  EXPECT_EQ(s270.getTrim(), -3);
    cs.setTrim(2);     EXPECT_EQ(cs.getTrim(), 2);
    sp.setTrim(10);    EXPECT_EQ(sp.getTrim(), 10);
    sh.setTrim(-5);    EXPECT_EQ(sh.getTrim(), -5);
    dc.setTrim(15);    EXPECT_EQ(dc.getTrim(), 15);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
