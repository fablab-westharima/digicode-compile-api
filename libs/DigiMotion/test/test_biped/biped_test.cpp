// DigiBiped host-side unit tests (Phase A-ε commit 2).
//
// Verifies the Layer 5 biped API end-to-end through mock IActuator-
// Channel instances. No HW dependencies: pure state-transition + mock
// channel observation. The actual sine motion math is exercised in
// test_sin_oscillator (Phase A-δ); these tests verify the integration
// (motion → ChannelBank → setTarget propagation, mode/state machine,
// per-channel forwarders, TrimStore boot-time push).
//
// 12 cases.
//
// Note: blocking variants (walkBlocking etc.) are no-ops on native per
// the #ifdef ARDUINO_ARCH_ESP32 polling pattern. Tests use *Async +
// manual tick(nowMs) to drive deterministic motion timelines.

#include <gtest/gtest.h>

#include <cstdio>
#include <map>
#include <string>

#include "DigiBiped.h"

namespace {

class MockChannel : public IActuatorChannel {
public:
    long lastSetTarget = -999;
    int  setTargetCalls = 0;
    int  lastSetTrim = -999;
    int  setTrimCalls = 0;
    int  lastPulseMin = -1;
    int  lastPulseMax = -1;
    int  setPulseRangeCalls = 0;
    int  lastMaxRate = -1;
    int  setMaxRateCalls = 0;
    int  attachCalls = 0;
    bool attached = false;
    bool reachedFlag = true;  // mocks "instantly reach" target by default

    bool attach() override { ++attachCalls; attached = true; return true; }
    void detach() override { attached = false; }
    bool isAttached() const override { return attached; }

    void setTarget(long target) override {
        lastSetTarget = target;
        ++setTargetCalls;
    }
    long getTarget() const override     { return lastSetTarget; }
    long getCurrent() const override    { return lastSetTarget; }
    bool hasReachedTarget() const override { return reachedFlag; }

    void setPulseRange(int minUs, int maxUs) override {
        lastPulseMin = minUs;
        lastPulseMax = maxUs;
        ++setPulseRangeCalls;
    }
    void setMaxRate(int rate) override {
        lastMaxRate = rate;
        ++setMaxRateCalls;
    }
    void setTrim(int v) override {
        lastSetTrim = v;
        ++setTrimCalls;
    }

    int getPulseMin() const override { return lastPulseMin; }
    int getPulseMax() const override { return lastPulseMax; }
    int getMaxRate() const override  { return lastMaxRate; }
    int getTrim() const override     { return lastSetTrim; }
    int getLastWrittenHw() const override { return 0; }

    void pump(unsigned long) override {}
    bool isActive() const override   { return attached && !reachedFlag; }
};

class MockNvsBackend {
public:
    std::map<std::string, int> data;
    void putInt(const char* key, int val) { data[std::string(key)] = val; }
    int getInt(const char* key, int def) const {
        auto it = data.find(std::string(key));
        return it != data.end() ? it->second : def;
    }
};

class TestTrimStore : public PortableTrimStore {
public:
    explicit TestTrimStore(MockNvsBackend& backend) : _backend(backend) {}
    void save() override {
        for (int pin = 0; pin < MAX_PIN; ++pin) {
            char key[8];
            std::snprintf(key, sizeof(key), "t%d", pin);
            _backend.putInt(key, _trims[pin]);
        }
    }
    void load() override {
        for (int pin = 0; pin < MAX_PIN; ++pin) {
            char key[8];
            std::snprintf(key, sizeof(key), "t%d", pin);
            _trims[pin] = _backend.getInt(key, 0);
        }
    }
private:
    MockNvsBackend& _backend;
};

}  // namespace

TEST(DigiBiped, ConstructorIsIdleNoChannels) {
    DigiBiped biped;
    EXPECT_EQ(biped.channelCount(), 0);
    EXPECT_EQ(biped.currentMotion(), DigiBiped::MOTION_IDLE);
    EXPECT_TRUE(biped.isIdle());
    EXPECT_EQ(biped.channelAt(0), nullptr);
    EXPECT_EQ(biped.channelAt(DigiBiped::CHANNEL_COUNT), nullptr);
}

TEST(DigiBiped, AttachChannelsPopulatesBankInOrder) {
    DigiBiped biped;
    MockChannel ll, rl, lf, rf;
    EXPECT_TRUE(biped.attachChannels(&ll, &rl, &lf, &rf));
    EXPECT_EQ(biped.channelCount(), 4);
    EXPECT_EQ(biped.channelAt(DigiBiped::LEFT_LEG),   &ll);
    EXPECT_EQ(biped.channelAt(DigiBiped::RIGHT_LEG),  &rl);
    EXPECT_EQ(biped.channelAt(DigiBiped::LEFT_FOOT),  &lf);
    EXPECT_EQ(biped.channelAt(DigiBiped::RIGHT_FOOT), &rf);
}

TEST(DigiBiped, AttachChannelsRejectsNullPointer) {
    DigiBiped biped;
    MockChannel ll, rl, lf;
    EXPECT_FALSE(biped.attachChannels(nullptr, &rl, &lf, &lf));
    EXPECT_FALSE(biped.attachChannels(&ll, nullptr, &lf, &lf));
    EXPECT_FALSE(biped.attachChannels(&ll, &rl, nullptr, &lf));
    EXPECT_FALSE(biped.attachChannels(&ll, &rl, &lf, nullptr));
    EXPECT_EQ(biped.channelCount(), 0);
}

TEST(DigiBiped, InitAttachesAllChannelsReturnsFalseIfNotAttached) {
    DigiBiped biped;
    EXPECT_FALSE(biped.init());  // no channels attached yet

    MockChannel ll, rl, lf, rf;
    biped.attachChannels(&ll, &rl, &lf, &rf);
    EXPECT_TRUE(biped.init());
    EXPECT_EQ(ll.attachCalls, 1);
    EXPECT_EQ(rl.attachCalls, 1);
    EXPECT_EQ(lf.attachCalls, 1);
    EXPECT_EQ(rf.attachCalls, 1);
}

// case 23 incident D + Session 143 lifecycle case c boot-time push:
// initWithTrim pulls trim from TrimStore per pin and pushes it through
// IActuatorChannel::setTrim() before motion starts.
TEST(DigiBiped, InitWithTrimAppliesPerPinFromStore) {
    DigiBiped biped;
    MockChannel ll, rl, lf, rf;
    biped.attachChannels(&ll, &rl, &lf, &rf);

    MockNvsBackend backend;
    backend.putInt("t27",  5);
    backend.putInt("t15", -8);
    backend.putInt("t14", 12);
    backend.putInt("t13", -3);
    TestTrimStore store(backend);
    store.load();

    EXPECT_TRUE(biped.initWithTrim(store, 27, 15, 14, 13));
    EXPECT_EQ(ll.lastSetTrim,  5);
    EXPECT_EQ(rl.lastSetTrim, -8);
    EXPECT_EQ(lf.lastSetTrim, 12);
    EXPECT_EQ(rf.lastSetTrim, -3);
}

TEST(DigiBiped, SetChannelTrimForwardsByIndex) {
    DigiBiped biped;
    MockChannel ll, rl, lf, rf;
    biped.attachChannels(&ll, &rl, &lf, &rf);

    biped.setChannelTrim(DigiBiped::LEFT_LEG,   3);
    biped.setChannelTrim(DigiBiped::RIGHT_LEG, -4);
    biped.setChannelTrim(DigiBiped::LEFT_FOOT,  7);
    biped.setChannelTrim(DigiBiped::RIGHT_FOOT, 0);

    EXPECT_EQ(ll.lastSetTrim,  3);
    EXPECT_EQ(rl.lastSetTrim, -4);
    EXPECT_EQ(lf.lastSetTrim,  7);
    EXPECT_EQ(rf.lastSetTrim,  0);

    // Out-of-range: silent no-op.
    biped.setChannelTrim(-1, 10);
    biped.setChannelTrim(DigiBiped::CHANNEL_COUNT, 10);
    EXPECT_EQ(ll.setTrimCalls, 1);  // unchanged
}

TEST(DigiBiped, SetChannelMaxRateAndPulseRangeForwardByIndex) {
    DigiBiped biped;
    MockChannel ll, rl, lf, rf;
    biped.attachChannels(&ll, &rl, &lf, &rf);

    biped.setChannelMaxRate(DigiBiped::LEFT_LEG, 60);
    biped.setChannelPulseRange(DigiBiped::RIGHT_FOOT, 500, 2500);

    EXPECT_EQ(ll.lastMaxRate, 60);
    EXPECT_EQ(rf.lastPulseMin, 500);
    EXPECT_EQ(rf.lastPulseMax, 2500);

    // Out-of-range no-op
    biped.setChannelMaxRate(99, 200);
    biped.setChannelPulseRange(-3, 100, 200);
    EXPECT_EQ(rl.lastMaxRate, -1);
    EXPECT_EQ(lf.lastPulseMin, -1);
}

TEST(DigiBiped, HomeAsyncTargetsAllChannelsToCenter) {
    DigiBiped biped;
    MockChannel ll, rl, lf, rf;
    biped.attachChannels(&ll, &rl, &lf, &rf);
    biped.init();

    biped.homeAsync(0);
    EXPECT_EQ(biped.currentMotion(), DigiBiped::MOTION_HOME);
    EXPECT_EQ(ll.lastSetTarget, DigiBiped::HOME_DEG);
    EXPECT_EQ(rl.lastSetTarget, DigiBiped::HOME_DEG);
    EXPECT_EQ(lf.lastSetTarget, DigiBiped::HOME_DEG);
    EXPECT_EQ(rf.lastSetTarget, DigiBiped::HOME_DEG);

    // tick after home publish: mocks have reachedFlag=true by default,
    // so bank.allReached() returns true → motion transitions to IDLE.
    biped.tick(10);
    EXPECT_TRUE(biped.isIdle());
}

TEST(DigiBiped, WalkAsyncTransitionsToWalkingState) {
    DigiBiped biped;
    MockChannel ll, rl, lf, rf;
    biped.attachChannels(&ll, &rl, &lf, &rf);
    biped.init();

    EXPECT_TRUE(biped.isIdle());
    biped.walkAsync(/*steps=*/2, /*direction=*/1, /*speed=*/60, /*nowMs=*/0);
    EXPECT_EQ(biped.currentMotion(), DigiBiped::MOTION_WALK);
    EXPECT_FALSE(biped.isIdle());
}

// Verify that tick() during walk publishes per-channel targets via the
// bank. Each tick triggers ONE setTargets call on the bank, which
// dispatches to 4 setTarget calls (one per channel).
TEST(DigiBiped, WalkAsyncTickAdvancesTargetsOnAllChannels) {
    DigiBiped biped;
    MockChannel ll, rl, lf, rf;
    biped.attachChannels(&ll, &rl, &lf, &rf);
    biped.init();
    // Reset post-init counters (init does not currently call setTarget).
    ll.setTargetCalls = rl.setTargetCalls = 0;
    lf.setTargetCalls = rf.setTargetCalls = 0;

    biped.walkAsync(/*steps=*/4, /*direction=*/1, /*speed=*/60, /*nowMs=*/0);

    // Drive 3 ticks at non-zero times. Each tick → 1 batch publish → 4
    // setTarget calls (one per channel).
    biped.tick(100);
    biped.tick(200);
    biped.tick(300);

    EXPECT_EQ(ll.setTargetCalls, 3);
    EXPECT_EQ(rl.setTargetCalls, 3);
    EXPECT_EQ(lf.setTargetCalls, 3);
    EXPECT_EQ(rf.setTargetCalls, 3);
    EXPECT_FALSE(biped.isIdle());  // still walking (4 cycles requested)
}

TEST(DigiBiped, TickCompletesMotionAfterRequestedCyclesAndReturnsToHome) {
    DigiBiped biped;
    MockChannel ll, rl, lf, rf;
    biped.attachChannels(&ll, &rl, &lf, &rf);
    biped.init();

    // speed=60, walk pattern max amp=25 → period = 4*25*1000/60 ≈ 1666 ms.
    // 2 cycles target → after t >= 2*1666 = 3332 ms, motion completes.
    biped.walkAsync(/*steps=*/2, /*direction=*/1, /*speed=*/60, /*nowMs=*/0);
    biped.tick(500);
    biped.tick(1500);
    EXPECT_FALSE(biped.isIdle());

    // Past 2 full cycles: motion transitions to IDLE and bank publishes
    // home targets.
    biped.tick(4000);
    EXPECT_TRUE(biped.isIdle());
    EXPECT_EQ(biped.currentMotion(), DigiBiped::MOTION_IDLE);
    EXPECT_EQ(ll.lastSetTarget, DigiBiped::HOME_DEG);
    EXPECT_EQ(rl.lastSetTarget, DigiBiped::HOME_DEG);
    EXPECT_EQ(lf.lastSetTarget, DigiBiped::HOME_DEG);
    EXPECT_EQ(rf.lastSetTarget, DigiBiped::HOME_DEG);
}

TEST(DigiBiped, StopDuringMotionReturnsToIdleAndHome) {
    DigiBiped biped;
    MockChannel ll, rl, lf, rf;
    biped.attachChannels(&ll, &rl, &lf, &rf);
    biped.init();

    biped.walkAsync(10, 1, 60, 0);
    biped.tick(100);
    EXPECT_FALSE(biped.isIdle());

    biped.stop();
    EXPECT_TRUE(biped.isIdle());
    EXPECT_EQ(biped.currentMotion(), DigiBiped::MOTION_IDLE);
    EXPECT_EQ(ll.lastSetTarget, DigiBiped::HOME_DEG);
    EXPECT_EQ(rl.lastSetTarget, DigiBiped::HOME_DEG);
    EXPECT_EQ(lf.lastSetTarget, DigiBiped::HOME_DEG);
    EXPECT_EQ(rf.lastSetTarget, DigiBiped::HOME_DEG);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
