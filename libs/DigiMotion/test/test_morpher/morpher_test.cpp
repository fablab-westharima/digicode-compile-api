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
    bool lastReverse = false;
    int  setReverseCalls = 0;
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
    void setReverse(bool r) override { lastReverse = r; ++setReverseCalls; }
    int getPulseMin() const override { return 0; }
    int getPulseMax() const override { return 0; }
    int getMaxRate() const override  { return 0; }
    int getTrim() const override     { return lastSetTrim; }
    bool getReverse() const override { return lastReverse; }
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

// Phase 3-B Session 156: per-channel reverse forward (transform robot
// mounting orientation correction compile-time path)
TEST(DigiMorpher, SetChannelReverseForwardsByIndex) {
    DigiMorpher m;
    MockChannel lh, rh, lf, rf;
    m.attachChannels(&lh, &rh, &lf, &rf);

    m.setChannelReverse(DigiMorpher::LEFT_HIP,    true);
    m.setChannelReverse(DigiMorpher::RIGHT_FOOT,  true);

    EXPECT_TRUE(lh.lastReverse);
    EXPECT_TRUE(rf.lastReverse);
    EXPECT_EQ(lh.setReverseCalls, 1);
    EXPECT_EQ(rf.setReverseCalls, 1);

    // Out-of-range: silent no-op.
    m.setChannelReverse(-1, true);
    EXPECT_EQ(lh.setReverseCalls, 1);  // unchanged
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

// Session 159 mirror-mount physics regression (mirrors the DigiBiped test):
// WALK drives both HIP channels in-phase (identical targets) for an
// alternating gait on a mirror-mounted frame; feet carry the ±offset bias.
TEST(DigiMorpher, WalkHipsAreInPhaseFeetCarryOffsetBias) {
    DigiMorpher m;
    MockChannel lh, rh, lf, rf;
    m.attachChannels(&lh, &rh, &lf, &rf);
    m.init();
    // Default mode is MORPH_WALK.
    EXPECT_TRUE(m.walkAsync(/*steps=*/4, /*direction=*/1, /*speed=*/60, /*nowMs=*/0));
    m.tick(366);

    EXPECT_EQ(lh.lastSetTarget, rh.lastSetTarget);          // hips in-phase
    EXPECT_NE(lh.lastSetTarget, DigiMorpher::HOME_DEG);     // motion happening
    EXPECT_EQ(lf.lastSetTarget - rf.lastSetTarget, 16);     // feet ±8 offset bias (Session 160 dynamic)
}

// Session 160 backlash redesign (mirrors the DigiBiped turn test): TURN drives
// the feet ANTI-phase so on the mirror mount they tilt the SAME physical
// direction (τ_L = τ_R ⇔ lf + rf folds to 2*HOME), rolling the body one way so
// the swing-side toe lifts. A toe-down swing foot scrapes under gear backlash.
// Contrast WALK's in-phase feet (lf - rf constant). Default mode is MORPH_WALK;
// turn is a walk-mode motion.
TEST(DigiMorpher, TurnFeetRollSamePhysicalDirection) {
    DigiMorpher m;
    MockChannel lh, rh, lf, rf;
    m.attachChannels(&lh, &rh, &lf, &rf);
    m.init();

    EXPECT_TRUE(m.turnAsync(/*steps=*/4, /*direction=*/1, /*speed=*/60, /*nowMs=*/0));
    m.tick(366);

    const long footSum = lf.lastSetTarget + rf.lastSetTarget;
    EXPECT_GE(footSum, 2 * DigiMorpher::HOME_DEG - 2);   // anti-phase feet fold to ~2*HOME
    EXPECT_LE(footSum, 2 * DigiMorpher::HOME_DEG + 2);
    EXPECT_NE(lf.lastSetTarget, rf.lastSetTarget);       // feet move oppositely
    EXPECT_NE(lh.lastSetTarget, rh.lastSetTarget);       // hips asymmetric → turn handedness
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
