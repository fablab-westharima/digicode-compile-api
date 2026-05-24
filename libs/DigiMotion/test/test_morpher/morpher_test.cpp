// DigiMorpher host-side unit tests (Phase A-ε commit 2).
//
// Verifies the Layer 5 transformable robot API: dual-mode (walk / roll)
// state machine, mode-guard rejection, motion propagation through
// ChannelBank, per-channel 3-axis forwarders, TrimStore boot-time push.
//
// 8 cases.

#include <gtest/gtest.h>

#include <cstdio>
#include <map>
#include <string>

#include "DigiMorpher.h"

namespace {

class MockChannel : public IActuatorChannel {
public:
    long lastSetTarget = -999;
    int  setTargetCalls = 0;
    int  lastSetTrim = -999;
    int  setTrimCalls = 0;
    int  attachCalls = 0;
    bool attached = false;
    bool reachedFlag = true;

    bool attach() override { ++attachCalls; attached = true; return true; }
    void detach() override { attached = false; }
    bool isAttached() const override { return attached; }
    void setTarget(long target) override { lastSetTarget = target; ++setTargetCalls; }
    long getTarget() const override { return lastSetTarget; }
    long getCurrent() const override { return lastSetTarget; }
    bool hasReachedTarget() const override { return reachedFlag; }
    void setPulseRange(int, int) override {}
    void setMaxRate(int) override {}
    void setTrim(int v) override { lastSetTrim = v; ++setTrimCalls; }
    int getPulseMin() const override { return 0; }
    int getPulseMax() const override { return 0; }
    int getMaxRate() const override  { return 0; }
    int getTrim() const override     { return lastSetTrim; }
    int getLastWrittenHw() const override { return 0; }
    void pump(unsigned long) override {}
    bool isActive() const override { return attached && !reachedFlag; }
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

TEST(DigiMorpher, ConstructorIsIdleWalkModeDefault) {
    DigiMorpher m;
    EXPECT_EQ(m.channelCount(), 0);
    EXPECT_TRUE(m.isIdle());
    EXPECT_EQ(m.currentMotion(), DigiMorpher::MOTION_IDLE);
    EXPECT_EQ(m.mode(), DigiMorpher::MORPH_WALK);
}

TEST(DigiMorpher, AttachChannelsPopulatesBankInOrder) {
    DigiMorpher m;
    MockChannel lh, rh, lf, rf;
    EXPECT_TRUE(m.attachChannels(&lh, &rh, &lf, &rf));
    EXPECT_EQ(m.channelCount(), 4);
    EXPECT_EQ(m.channelAt(DigiMorpher::LEFT_HIP),   &lh);
    EXPECT_EQ(m.channelAt(DigiMorpher::RIGHT_HIP),  &rh);
    EXPECT_EQ(m.channelAt(DigiMorpher::LEFT_FOOT),  &lf);
    EXPECT_EQ(m.channelAt(DigiMorpher::RIGHT_FOOT), &rf);
}

TEST(DigiMorpher, SetModeSwitchesStateNoMotion) {
    DigiMorpher m;
    EXPECT_EQ(m.mode(), DigiMorpher::MORPH_WALK);
    m.setMode(DigiMorpher::MORPH_ROLL);
    EXPECT_EQ(m.mode(), DigiMorpher::MORPH_ROLL);
    EXPECT_TRUE(m.isIdle());  // setMode alone does not trigger motion
    m.setMode(DigiMorpher::MORPH_WALK);
    EXPECT_EQ(m.mode(), DigiMorpher::MORPH_WALK);
}

// case 23 incident D: per-pin trim from TrimStore reaches the right
// hip/foot channels at init time.
TEST(DigiMorpher, InitWithTrimAppliesPerPinFromStore) {
    DigiMorpher m;
    MockChannel lh, rh, lf, rf;
    m.attachChannels(&lh, &rh, &lf, &rf);

    MockNvsBackend backend;
    backend.putInt("t26",  6);
    backend.putInt("t25", -9);
    backend.putInt("t24", 11);
    backend.putInt("t23", -2);
    TestTrimStore store(backend);
    store.load();

    EXPECT_TRUE(m.initWithTrim(store, 26, 25, 24, 23));
    EXPECT_EQ(lh.lastSetTrim,  6);
    EXPECT_EQ(rh.lastSetTrim, -9);
    EXPECT_EQ(lf.lastSetTrim, 11);
    EXPECT_EQ(rf.lastSetTrim, -2);
}

TEST(DigiMorpher, SetChannelTrimForwardsByIndex) {
    DigiMorpher m;
    MockChannel lh, rh, lf, rf;
    m.attachChannels(&lh, &rh, &lf, &rf);

    m.setChannelTrim(DigiMorpher::LEFT_HIP,    4);
    m.setChannelTrim(DigiMorpher::RIGHT_FOOT, -5);

    EXPECT_EQ(lh.lastSetTrim,  4);
    EXPECT_EQ(rf.lastSetTrim, -5);
}

// Mode guard: walk-mode motion in roll mode is rejected (returns false,
// no state transition, no channel publish).
TEST(DigiMorpher, WalkAsyncRejectedInRollMode) {
    DigiMorpher m;
    MockChannel lh, rh, lf, rf;
    m.attachChannels(&lh, &rh, &lf, &rf);
    m.init();
    m.setMode(DigiMorpher::MORPH_ROLL);

    EXPECT_FALSE(m.walkAsync(2, 1, 60, 0));
    EXPECT_TRUE(m.isIdle());
    EXPECT_EQ(m.currentMotion(), DigiMorpher::MOTION_IDLE);
    EXPECT_EQ(lh.setTargetCalls, 0);
}

TEST(DigiMorpher, RollAsyncDrivesTargetsInRollMode) {
    DigiMorpher m;
    MockChannel lh, rh, lf, rf;
    m.attachChannels(&lh, &rh, &lf, &rf);
    m.init();
    m.setMode(DigiMorpher::MORPH_ROLL);

    EXPECT_TRUE(m.rollAsync(2, 1, 60, 0));
    EXPECT_EQ(m.currentMotion(), DigiMorpher::MOTION_ROLL);
    EXPECT_FALSE(m.isIdle());

    // Reset counters; tick should publish through bank.
    lh.setTargetCalls = rh.setTargetCalls = 0;
    lf.setTargetCalls = rf.setTargetCalls = 0;
    m.tick(100);
    m.tick(200);
    EXPECT_EQ(lh.setTargetCalls, 2);
    EXPECT_EQ(rh.setTargetCalls, 2);
    EXPECT_EQ(lf.setTargetCalls, 2);
    EXPECT_EQ(rf.setTargetCalls, 2);

    // Roll-mode walk reject (verify the inverse mode guard holds).
    EXPECT_FALSE(m.walkAsync(1, 1, 60, 0));
}

TEST(DigiMorpher, StopReturnsToIdleAndHome) {
    DigiMorpher m;
    MockChannel lh, rh, lf, rf;
    m.attachChannels(&lh, &rh, &lf, &rf);
    m.init();

    m.walkAsync(5, 1, 60, 0);
    m.tick(100);
    EXPECT_FALSE(m.isIdle());

    m.stop();
    EXPECT_TRUE(m.isIdle());
    EXPECT_EQ(m.currentMotion(), DigiMorpher::MOTION_IDLE);
    EXPECT_EQ(lh.lastSetTarget, DigiMorpher::HOME_DEG);
    EXPECT_EQ(rh.lastSetTarget, DigiMorpher::HOME_DEG);
    EXPECT_EQ(lf.lastSetTarget, DigiMorpher::HOME_DEG);
    EXPECT_EQ(rf.lastSetTarget, DigiMorpher::HOME_DEG);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
