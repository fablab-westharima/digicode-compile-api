// DigiRover host-side unit tests (Phase A-ε commit 2).
//
// Verifies the Layer 5 dual-mode rover API: mutually-exclusive
// init*Mode selection, per-mode velocity command mapping (continuous-
// rotation servo vs H-bridge DC motor), trim forwarding.
//
// 6 cases. Native env, no HW.

#include <gtest/gtest.h>

#include "DigiRover.h"

namespace {

class MockChannel : public IActuatorChannel {
public:
    long lastSetTarget = -999;
    int  setTargetCalls = 0;
    int  lastSetTrim = -999;
    int  setTrimCalls = 0;
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
    int getPulseMin() const override { return 0; }
    int getPulseMax() const override { return 0; }
    int getMaxRate() const override  { return 0; }
    int getTrim() const override     { return lastSetTrim; }
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

TEST(DigiRover, InitDcMotorModeAttachesFourChannels) {
    DigiRover r;
    MockChannel la, lb, ra, rb;
    EXPECT_TRUE(r.initDcMotorMode(&la, &lb, &ra, &rb));
    EXPECT_EQ(r.mode(), DigiRover::ROVER_DC_MOTOR_4PIN);
    EXPECT_EQ(r.channelCount(), 4);
    EXPECT_EQ(la.attachCalls, 1);
    EXPECT_EQ(lb.attachCalls, 1);
    EXPECT_EQ(ra.attachCalls, 1);
    EXPECT_EQ(rb.attachCalls, 1);
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

// DC motor H-bridge: forward puts magnitude on A pin, B pin to 0.
// Backward puts magnitude on B pin, A pin to 0. Stop: both 0.
TEST(DigiRover, ForwardBackwardOnDcMotorModeUsesHBridgeMapping) {
    DigiRover r;
    MockChannel la, lb, ra, rb;
    r.initDcMotorMode(&la, &lb, &ra, &rb);

    r.forward(60);
    EXPECT_EQ(la.lastSetTarget, 60);
    EXPECT_EQ(lb.lastSetTarget, 0);
    EXPECT_EQ(ra.lastSetTarget, 60);
    EXPECT_EQ(rb.lastSetTarget, 0);
    EXPECT_TRUE(r.isMoving());

    r.backward(40);
    EXPECT_EQ(la.lastSetTarget, 0);
    EXPECT_EQ(lb.lastSetTarget, 40);
    EXPECT_EQ(ra.lastSetTarget, 0);
    EXPECT_EQ(rb.lastSetTarget, 40);

    r.stop();
    EXPECT_EQ(la.lastSetTarget, 0);
    EXPECT_EQ(lb.lastSetTarget, 0);
    EXPECT_EQ(ra.lastSetTarget, 0);
    EXPECT_EQ(rb.lastSetTarget, 0);
    EXPECT_FALSE(r.isMoving());
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

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
