// PortableBackgroundPump host-side unit tests (Phase A-γ commit 1).
//
// Verifies the platform-agnostic registry + scan logic that backs both
// PortableBackgroundPump and (via inheritance) FreeRtosBackgroundPump.
// FreeRTOS itself is not exercised here; Phase E performs hardware smoke.
//
// 7 cases covering: register + tick / unregister / inactive skip /
// duplicate reject / nullptr reject / overflow / multi-entry iteration.

#include <gtest/gtest.h>

#include "pump/IBackgroundPump.h"

namespace {

class MockPumpable : public IPumpable {
public:
    int pumpCallCount = 0;
    unsigned long lastNowMs = 0;
    bool active = true;

    void pump(unsigned long nowMs) override {
        ++pumpCallCount;
        lastNowMs = nowMs;
    }
    bool isActive() const override { return active; }
};

} // namespace

TEST(BackgroundPump, RegisterIncrementsCountAndTickDrivesPumpable) {
    PortableBackgroundPump pump;
    MockPumpable a;
    ASSERT_EQ(pump.registeredCount(), 0);
    ASSERT_TRUE(pump.registerPumpable(&a));
    EXPECT_EQ(pump.registeredCount(), 1);

    pump.tick(123);
    EXPECT_EQ(a.pumpCallCount, 1);
    EXPECT_EQ(a.lastNowMs, 123u);
}

TEST(BackgroundPump, UnregisterRemovesAndPreventsFurtherTicks) {
    PortableBackgroundPump pump;
    MockPumpable a;
    pump.registerPumpable(&a);
    pump.tick(10);
    ASSERT_EQ(a.pumpCallCount, 1);

    ASSERT_TRUE(pump.unregisterPumpable(&a));
    EXPECT_EQ(pump.registeredCount(), 0);

    pump.tick(20);
    EXPECT_EQ(a.pumpCallCount, 1);  // unchanged after unregister
}

TEST(BackgroundPump, TickSkipsInactivePumpables) {
    PortableBackgroundPump pump;
    MockPumpable a;
    a.active = false;
    pump.registerPumpable(&a);

    pump.tick(50);
    EXPECT_EQ(a.pumpCallCount, 0);

    a.active = true;
    pump.tick(60);
    EXPECT_EQ(a.pumpCallCount, 1);
    EXPECT_EQ(a.lastNowMs, 60u);
}

TEST(BackgroundPump, RegisterRejectsDuplicate) {
    PortableBackgroundPump pump;
    MockPumpable a;
    ASSERT_TRUE(pump.registerPumpable(&a));
    EXPECT_FALSE(pump.registerPumpable(&a));
    EXPECT_EQ(pump.registeredCount(), 1);
}

TEST(BackgroundPump, RegisterRejectsNullptr) {
    PortableBackgroundPump pump;
    EXPECT_FALSE(pump.registerPumpable(nullptr));
    EXPECT_EQ(pump.registeredCount(), 0);
    EXPECT_FALSE(pump.unregisterPumpable(nullptr));
}

TEST(BackgroundPump, RegistryFullReturnsFalseOnOverflow) {
    PortableBackgroundPump pump;
    MockPumpable pumpables[IBackgroundPump::MAX_ENTRIES + 1];
    for (int i = 0; i < IBackgroundPump::MAX_ENTRIES; ++i) {
        ASSERT_TRUE(pump.registerPumpable(&pumpables[i])) << "i=" << i;
    }
    EXPECT_EQ(pump.registeredCount(), IBackgroundPump::MAX_ENTRIES);
    EXPECT_FALSE(pump.registerPumpable(&pumpables[IBackgroundPump::MAX_ENTRIES]));
    EXPECT_EQ(pump.registeredCount(), IBackgroundPump::MAX_ENTRIES);
}

TEST(BackgroundPump, TickIteratesAllRegisteredActiveEntries) {
    PortableBackgroundPump pump;
    MockPumpable a, b, c;
    pump.registerPumpable(&a);
    pump.registerPumpable(&b);
    pump.registerPumpable(&c);
    b.active = false;

    pump.tick(99);
    EXPECT_EQ(a.pumpCallCount, 1);
    EXPECT_EQ(b.pumpCallCount, 0);
    EXPECT_EQ(c.pumpCallCount, 1);
    EXPECT_EQ(a.lastNowMs, 99u);
    EXPECT_EQ(c.lastNowMs, 99u);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
