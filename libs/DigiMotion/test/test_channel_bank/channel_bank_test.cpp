// ChannelBank host-side unit tests (Phase A-δ).
//
// Verifies the Layer 3 grouping + barrier logic:
//   - add / remove / count / at
//   - nullptr + duplicate + full registry rejection
//   - setTarget(index) + setTargets(batch) propagation to held channels
//   - allReached() AND-of-hasReachedTarget predicate
//
// MockChannel implements the IActuatorChannel surface with deterministic
// last-write + reached state observation, no HW dependencies. The bank
// does not pump channels itself (Layer 1 BackgroundPump owns that role),
// so pump() / isActive() are no-ops here.
//
// 10 cases. Layer 3 platform-agnostic: no Arduino.h / ESP32 API in this
// file or the source it tests.

#include <gtest/gtest.h>

#include "motion/ChannelBank.h"

namespace {

class MockChannel : public IActuatorChannel {
public:
    long lastSetTarget = -999;
    int setTargetCalls = 0;
    bool reached = false;

    bool attach() override { return true; }
    void detach() override {}
    bool isAttached() const override { return true; }

    void setTarget(long target) override {
        lastSetTarget = target;
        ++setTargetCalls;
    }
    long getTarget() const override     { return lastSetTarget; }
    long getCurrent() const override    { return lastSetTarget; }
    bool hasReachedTarget() const override { return reached; }

    void setPulseRange(int, int) override {}
    void setMaxRate(int) override {}
    void setTrim(int) override {}
    int getPulseMin() const override { return 0; }
    int getPulseMax() const override { return 0; }
    int getMaxRate() const override  { return 0; }
    int getTrim() const override     { return 0; }
    int getLastWrittenHw() const override { return 0; }

    void pump(unsigned long) override {}
    bool isActive() const override   { return true; }
};

}  // namespace

TEST(ChannelBank, DefaultConstructionIsEmpty) {
    ChannelBank bank;
    EXPECT_EQ(bank.count(), 0);
    EXPECT_EQ(bank.at(0), nullptr);
    EXPECT_EQ(bank.at(-1), nullptr);
    EXPECT_EQ(bank.at(ChannelBank::MAX_CHANNELS), nullptr);
    EXPECT_TRUE(bank.allReached());  // vacuously true with 0 channels
}

TEST(ChannelBank, AddChannelIncrementsCountAndExposesViaAt) {
    ChannelBank bank;
    MockChannel a, b;
    ASSERT_TRUE(bank.addChannel(&a));
    EXPECT_EQ(bank.count(), 1);
    EXPECT_EQ(bank.at(0), &a);

    ASSERT_TRUE(bank.addChannel(&b));
    EXPECT_EQ(bank.count(), 2);
    EXPECT_EQ(bank.at(1), &b);
}

TEST(ChannelBank, AddChannelRejectsNullptr) {
    ChannelBank bank;
    EXPECT_FALSE(bank.addChannel(nullptr));
    EXPECT_EQ(bank.count(), 0);
    EXPECT_FALSE(bank.removeChannel(nullptr));
}

TEST(ChannelBank, AddChannelRejectsDuplicate) {
    ChannelBank bank;
    MockChannel a;
    ASSERT_TRUE(bank.addChannel(&a));
    EXPECT_FALSE(bank.addChannel(&a));
    EXPECT_EQ(bank.count(), 1);
}

TEST(ChannelBank, AddChannelRejectsWhenFull) {
    ChannelBank bank;
    MockChannel channels[ChannelBank::MAX_CHANNELS + 1];
    for (int i = 0; i < ChannelBank::MAX_CHANNELS; ++i) {
        ASSERT_TRUE(bank.addChannel(&channels[i])) << "i=" << i;
    }
    EXPECT_EQ(bank.count(), ChannelBank::MAX_CHANNELS);
    EXPECT_FALSE(bank.addChannel(&channels[ChannelBank::MAX_CHANNELS]));
    EXPECT_EQ(bank.count(), ChannelBank::MAX_CHANNELS);
}

TEST(ChannelBank, RemoveChannelDecrementsAndPreventsFurtherWrites) {
    ChannelBank bank;
    MockChannel a, b;
    bank.addChannel(&a);
    bank.addChannel(&b);

    ASSERT_TRUE(bank.removeChannel(&a));
    EXPECT_EQ(bank.count(), 1);

    // Removed channels are not pushed by setTargets / setTarget.
    long targets[1] = {42};
    bank.setTargets(targets, 1);
    EXPECT_EQ(a.setTargetCalls, 0);
    EXPECT_EQ(b.setTargetCalls, 1);
    EXPECT_EQ(b.lastSetTarget, 42);

    EXPECT_FALSE(bank.removeChannel(&a));  // already removed
}

TEST(ChannelBank, SetTargetByIndexPropagatesToChannel) {
    ChannelBank bank;
    MockChannel a, b;
    bank.addChannel(&a);
    bank.addChannel(&b);

    bank.setTarget(0, 100);
    EXPECT_EQ(a.lastSetTarget, 100);
    EXPECT_EQ(b.setTargetCalls, 0);

    bank.setTarget(1, 200);
    EXPECT_EQ(b.lastSetTarget, 200);

    // Out-of-range is silently ignored.
    bank.setTarget(-1, 9999);
    bank.setTarget(ChannelBank::MAX_CHANNELS, 9999);
    EXPECT_EQ(a.lastSetTarget, 100);
    EXPECT_EQ(b.lastSetTarget, 200);
}

TEST(ChannelBank, SetTargetsBatchPropagatesInOrderToRegisteredChannels) {
    ChannelBank bank;
    MockChannel a, b, c;
    bank.addChannel(&a);
    bank.addChannel(&b);
    bank.addChannel(&c);

    long targets[3] = {11, 22, 33};
    bank.setTargets(targets, 3);
    EXPECT_EQ(a.lastSetTarget, 11);
    EXPECT_EQ(b.lastSetTarget, 22);
    EXPECT_EQ(c.lastSetTarget, 33);
    EXPECT_EQ(a.setTargetCalls, 1);
    EXPECT_EQ(b.setTargetCalls, 1);
    EXPECT_EQ(c.setTargetCalls, 1);

    // Shorter array updates a prefix only.
    long shorter[2] = {77, 88};
    bank.setTargets(shorter, 2);
    EXPECT_EQ(a.lastSetTarget, 77);
    EXPECT_EQ(b.lastSetTarget, 88);
    EXPECT_EQ(c.lastSetTarget, 33);  // unchanged
}

TEST(ChannelBank, AllReachedTrueWhenEveryChannelReached) {
    ChannelBank bank;
    MockChannel a, b, c;
    bank.addChannel(&a);
    bank.addChannel(&b);
    bank.addChannel(&c);

    a.reached = true;
    b.reached = true;
    c.reached = true;
    EXPECT_TRUE(bank.allReached());
}

TEST(ChannelBank, AllReachedFalseWhenAnyChannelNotReached) {
    ChannelBank bank;
    MockChannel a, b, c;
    bank.addChannel(&a);
    bank.addChannel(&b);
    bank.addChannel(&c);

    a.reached = true;
    b.reached = false;  // one straggler is enough
    c.reached = true;
    EXPECT_FALSE(bank.allReached());

    b.reached = true;
    EXPECT_TRUE(bank.allReached());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
