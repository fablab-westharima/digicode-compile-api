// DigiRover host-side unit tests (Phase A-ε commit 2;
//   Phase X-1.5 Q-D=A refactor: DC motor mode now uses 2 DcMotorChannel
//   instances instead of 4 single-direction IActuatorChannel slots).
//
// Verifies the Layer 5 dual-mode rover API: mutually-exclusive
// init*Mode selection, per-mode velocity command mapping (continuous-
// rotation servo with right-side mirror vs sign-encoded DC motor),
// trim forwarding.
//
// 6 cases. Native env, no HW.

#include <gtest/gtest.h>

#include "DigiRover.h"
#include "actuator/DcMotorChannel.h"

namespace {

class MockChannel : public IActuatorChannel {
public:
    long lastSetTarget = -999;
    int  setTargetCalls = 0;
    int  lastSetTrim = -999;
    int  setTrimCalls = 0;
    bool lastReverse = false;
    int  setReverseCalls = 0;
    int  attachCalls = 0;
    bool attached = false;

    bool attach() override { ++attachCalls; attached = true; return true; }
    void detach() override { attached = false; }
    bool isAttached() const override { return attached; }
    void setTarget(long target) override { lastSetTarget = target; ++setTargetCalls; }
    long getTarget() const override { return lastSetTarget; }
    long getCurrent() const override { return lastSetTarget; }
    bool hasReachedTarget() const override { return true; }
    void setPulseRange(int, int) override {}
    void setMaxRate(int) override {}
    void setTrim(int v) override { lastSetTrim = v; ++setTrimCalls; }
    void setReverse(bool r) override { lastReverse = r; ++setReverseCalls; }
    int getPulseMin() const override { return 0; }
    int getPulseMax() const override { return 0; }
    int getMaxRate() const override  { return 0; }
    int getTrim() const override     { return lastSetTrim; }
    bool getReverse() const override { return lastReverse; }
    int getLastWrittenHw() const override { return 0; }
    void pump(unsigned long) override {}
    bool isActive() const override { return false; }
};

}  // namespace

TEST(DigiRover, ConstructorIsUnsetMode) {
    DigiRover r;
    EXPECT_EQ(r.mode(), DigiRover::ROVER_UNSET);
    EXPECT_EQ(r.channelCount(), 0);
    EXPECT_FALSE(r.isMoving());
}

TEST(DigiRover, InitServoModeAttachesTwoChannels) {
    DigiRover r;
    MockChannel left, right;
    EXPECT_TRUE(r.initServoMode(&left, &right));
    EXPECT_EQ(r.mode(), DigiRover::ROVER_SERVO_2PIN);
    EXPECT_EQ(r.channelCount(), 2);
    EXPECT_EQ(left.attachCalls, 1);
    EXPECT_EQ(right.attachCalls, 1);
    EXPECT_EQ(r.channelAt(DigiRover::LEFT_SERVO),  &left);
    EXPECT_EQ(r.channelAt(DigiRover::RIGHT_SERVO), &right);

    // nullptr rejected
    DigiRover r2;
    EXPECT_FALSE(r2.initServoMode(nullptr, &right));
    EXPECT_EQ(r2.mode(), DigiRover::ROVER_UNSET);
}

TEST(DigiRover, InitDcMotorModeAttachesTwoMotors) {
    DigiRover r;
    DcMotorChannel leftMotor(/*fwd*/16, /*rev*/17);
    DcMotorChannel rightMotor(/*fwd*/18, /*rev*/19);
    EXPECT_TRUE(r.initDcMotorMode(&leftMotor, &rightMotor));
    EXPECT_EQ(r.mode(), DigiRover::ROVER_DC_MOTOR_4PIN);
    EXPECT_EQ(r.channelCount(), 2);
    EXPECT_TRUE(leftMotor.isAttached());
    EXPECT_TRUE(rightMotor.isAttached());
    EXPECT_EQ(r.channelAt(DigiRover::LEFT_DC_MOTOR),  &leftMotor);
    EXPECT_EQ(r.channelAt(DigiRover::RIGHT_DC_MOTOR), &rightMotor);

    // nullptr rejected
    DigiRover r2;
    EXPECT_FALSE(r2.initDcMotorMode(nullptr, &rightMotor));
    EXPECT_EQ(r2.mode(), DigiRover::ROVER_UNSET);
}

// Per-mode mapping: servo 2-pin uses signed speeds with right side
// mirrored (right forward = -speed because of opposite servo mounting).
TEST(DigiRover, ForwardOnServoModeMirrorsRightSide) {
    DigiRover r;
    MockChannel left, right;
    r.initServoMode(&left, &right);
    left.setTargetCalls = right.setTargetCalls = 0;

    r.forward(50);
    EXPECT_EQ(left.lastSetTarget,   50);
    EXPECT_EQ(right.lastSetTarget, -50);  // mirrored
    EXPECT_TRUE(r.isMoving());

    r.stop();
    EXPECT_EQ(left.lastSetTarget,  0);
    EXPECT_EQ(right.lastSetTarget, 0);
    EXPECT_FALSE(r.isMoving());

    // spinLeft: left back, right forward
    r.spinLeft(40);
    EXPECT_EQ(left.lastSetTarget,  -40);
    EXPECT_EQ(right.lastSetTarget, -40);  // right forward = -(+40)
}

// DC motor: DcMotorChannel encodes direction in the sign of setTarget
// internally. Session 159: the right motor is sign-mirrored in _drive,
// matching servo mode (both wheels mounted facing each other), so
// forward(60) → leftMotor.target = +60, rightMotor.target = -60.
TEST(DigiRover, ForwardBackwardOnDcMotorModeUsesSignedTarget) {
    DigiRover r;
    DcMotorChannel leftMotor(16, 17);
    DcMotorChannel rightMotor(18, 19);
    r.initDcMotorMode(&leftMotor, &rightMotor);

    r.forward(60);
    EXPECT_EQ(leftMotor.getTarget(),  60);
    EXPECT_EQ(rightMotor.getTarget(), -60);  // mirrored (Session 159)
    EXPECT_TRUE(r.isMoving());

    r.backward(40);
    EXPECT_EQ(leftMotor.getTarget(),  -40);
    EXPECT_EQ(rightMotor.getTarget(),  40);  // mirrored

    r.stop();
    EXPECT_EQ(leftMotor.getTarget(),  0);
    EXPECT_EQ(rightMotor.getTarget(), 0);
    EXPECT_FALSE(r.isMoving());

    // Spin-in-place (left): _drive(-40, +40); right is sign-mirrored →
    // rightMotor sees -40, both motors same sign for an in-place spin.
    r.spinLeft(40);
    EXPECT_EQ(leftMotor.getTarget(),  -40);
    EXPECT_EQ(rightMotor.getTarget(), -40);  // -(+40) mirrored
}

TEST(DigiRover, SetChannelTrimForwardsToIndexedChannel) {
    DigiRover r;
    MockChannel left, right;
    r.initServoMode(&left, &right);

    r.setChannelTrim(DigiRover::LEFT_SERVO,   5);
    r.setChannelTrim(DigiRover::RIGHT_SERVO, -3);
    EXPECT_EQ(left.lastSetTrim,   5);
    EXPECT_EQ(right.lastSetTrim, -3);

    // Speed clamping: forward(150) clamps to 100, forward(-10) clamps to 0
    left.setTargetCalls = right.setTargetCalls = 0;
    r.forward(150);
    EXPECT_EQ(left.lastSetTarget,   100);
    EXPECT_EQ(right.lastSetTarget, -100);
    r.forward(-10);
    EXPECT_EQ(left.lastSetTarget,   0);
    EXPECT_EQ(right.lastSetTarget,  0);
}

// Phase 3-B Session 156: per-channel reverse forward on both servo + dc
// motor modes (compile-time wheel mounting orientation correction). Each
// mode uses different concrete IActuatorChannel — IF dispatch verified.
TEST(DigiRover, SetChannelReverseForwardsToIndexedChannel) {
    DigiRover r;
    MockChannel left, right;
    r.initServoMode(&left, &right);

    r.setChannelReverse(DigiRover::LEFT_SERVO,  true);
    r.setChannelReverse(DigiRover::RIGHT_SERVO, false);
    EXPECT_TRUE(left.lastReverse);
    EXPECT_FALSE(right.lastReverse);
    EXPECT_EQ(left.setReverseCalls,  1);
    EXPECT_EQ(right.setReverseCalls, 1);

    // Out-of-range: silent no-op (mirrors setChannelTrim contract).
    r.setChannelReverse(-1, true);
    r.setChannelReverse(DigiRover::MAX_CHANNELS, true);
    EXPECT_EQ(left.setReverseCalls, 1);  // unchanged
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
