// GestureLibrary + DigiBiped::playGesture host-side unit tests
// (Phase A-ε-2 commit 2, D-new-1a).
//
// Verifies:
//   - GestureLibrary: 14 design intents (GESTURE_GREETING ..
//     GESTURE_ERROR_ALERT) are populated with cycles > 0 + periodMs > 0;
//     GESTURE_NONE sentinel is empty; out-of-range id is safe.
//   - Specific gesture-to-sound bindings for representative entries.
//   - DigiBiped::attachBuzzer lifecycle (attach + detach via nullptr).
//   - DigiBiped::playGesture flow: motion transitions to MOTION_GESTURE,
//     bank publishes oscillator targets each tick, motion completes
//     after the cycle count elapses, sound dispatches through the
//     attached buzzer (silent gestures bypass the buzzer cleanly).
//   - playGesture without attached buzzer: motion still runs; no crash.
//   - playGesture(GESTURE_NONE): full no-op (no motion, no sound).
//
// 14 cases. Layer 4 platform-agnostic: no Arduino.h / ESP32 API.
//
// Anti-derivation: 60.md §4 lists canonical T5 grep gates. This file
// uses only the new GESTURE_<intent> + BEEP_<intent> identifier
// families; no upstream literal.

#include <gtest/gtest.h>

#include <vector>

#include "DigiBiped.h"
#include "sound/GestureLibrary.h"
#include "sound/IBuzzer.h"
#include "sound/SoundPresetTable.h"

namespace {

// Recording buzzer mock — same shape as test_buzzer's RecordingBuzzer
// but defined locally so this test file is self-contained (test files do
// not share TUs in PIO test discovery).
class RecordingBuzzer : public PortableBuzzer {
public:
    std::vector<SoundPresetId> presets;
    std::vector<int> tones;
    std::vector<int> bends;

    void playTone(int freqHz, int /*durationMs*/) override {
        tones.push_back(freqHz);
    }
    void playBendTone(int initFreq, int /*endFreq*/, int /*totalDurationMs*/) override {
        bends.push_back(initFreq);
    }
    void stop() override {}

    // Override playPreset to record at the preset level too, so tests can
    // verify "preset X was triggered" without inspecting tone-step
    // counts (which depend on table contents that may iterate-refine).
    void playPreset(SoundPresetId presetId) override {
        presets.push_back(presetId);
        PortableBuzzer::playPreset(presetId);
    }
};

// Minimal mock channel — same shape as test_biped's MockChannel.
class MockChannel : public IActuatorChannel {
public:
    long lastSetTarget = -999;
    int  setTargetCalls = 0;
    bool reachedFlag = true;

    bool attach() override { return true; }
    void detach() override {}
    bool isAttached() const override { return true; }

    void setTarget(long target) override {
        lastSetTarget = target;
        ++setTargetCalls;
    }
    long getTarget() const override     { return lastSetTarget; }
    long getCurrent() const override    { return lastSetTarget; }
    bool hasReachedTarget() const override { return reachedFlag; }

    void setPulseRange(int, int) override {}
    void setMaxRate(int) override {}
    void setTrim(int) override {}

    int getPulseMin() const override { return 0; }
    int getPulseMax() const override { return 0; }
    int getMaxRate() const override  { return 0; }
    int getTrim() const override     { return 0; }
    int getLastWrittenHw() const override { return 0; }

    void pump(unsigned long) override {}
    bool isActive() const override { return false; }
};

}  // namespace

// === GestureLibrary table ===

TEST(GestureLibrary, NoneSentinelIsZeroCyclesNoSound) {
    const GestureDefinition& def = GestureLibrary::get(GESTURE_NONE);
    EXPECT_EQ(def.motion.cycles, 0);
    EXPECT_EQ(def.motion.periodMs, 0);
    EXPECT_EQ(def.sound, BEEP_NONE);
}

TEST(GestureLibrary, AllFourteenIntentsHaveSaneCycleAndPeriod) {
    for (int id = GESTURE_GREETING; id < GESTURE_COUNT; ++id) {
        const GestureDefinition& def =
            GestureLibrary::get(static_cast<GestureId>(id));
        EXPECT_GT(def.motion.cycles, 0)
            << "gesture id " << id << " has zero cycles";
        EXPECT_GT(def.motion.periodMs, 0)
            << "gesture id " << id << " has zero period";
        // sound is allowed to be BEEP_NONE (e.g. SEARCH / IDLE_BREATHING).
        EXPECT_GE(def.sound, 0);
        EXPECT_LT(def.sound, SOUND_PRESET_COUNT);
    }
}

TEST(GestureLibrary, OutOfRangeIdReturnsNoneSentinel) {
    const GestureDefinition& neg =
        GestureLibrary::get(static_cast<GestureId>(-1));
    EXPECT_EQ(neg.motion.cycles, 0);
    EXPECT_EQ(neg.sound, BEEP_NONE);

    const GestureDefinition& over =
        GestureLibrary::get(static_cast<GestureId>(GESTURE_COUNT));
    EXPECT_EQ(over.motion.cycles, 0);
    EXPECT_EQ(over.sound, BEEP_NONE);
}

TEST(GestureLibrary, SilentGestureSearchHasNoSoundBinding) {
    const GestureDefinition& def = GestureLibrary::get(GESTURE_SEARCH);
    EXPECT_EQ(def.sound, BEEP_NONE);
    EXPECT_GT(def.motion.cycles, 0);  // motion still runs
}

TEST(GestureLibrary, CheerGestureBindsFanfareSound) {
    const GestureDefinition& def = GestureLibrary::get(GESTURE_CHEER);
    EXPECT_EQ(def.sound, BEEP_FANFARE);
    EXPECT_GT(def.motion.cycles, 0);
}

TEST(GestureLibrary, ErrorAlertGestureBindsDescendingPattern) {
    const GestureDefinition& def = GestureLibrary::get(GESTURE_ERROR_ALERT);
    EXPECT_EQ(def.sound, BEEP_ERROR_DESCENDING);
}

// === DigiBiped::attachBuzzer ===

TEST(DigiBiped, AttachBuzzerStoresPointer) {
    DigiBiped biped;
    RecordingBuzzer buz;
    buz.attach(25);

    EXPECT_EQ(biped.attachedBuzzer(), nullptr);
    biped.attachBuzzer(&buz);
    EXPECT_EQ(biped.attachedBuzzer(), &buz);

    biped.attachBuzzer(nullptr);
    EXPECT_EQ(biped.attachedBuzzer(), nullptr);
}

// === DigiBiped::playGesture ===

TEST(DigiBiped, PlayGestureTransitionsToGestureMotion) {
    DigiBiped biped;
    MockChannel ll, rl, lf, rf;
    biped.attachChannels(&ll, &rl, &lf, &rf);
    biped.init();

    EXPECT_TRUE(biped.isIdle());
    biped.playGesture(GESTURE_YES, /*nowMs=*/0);
    EXPECT_EQ(biped.currentMotion(), DigiBiped::MOTION_GESTURE);
    EXPECT_FALSE(biped.isIdle());
}

TEST(DigiBiped, PlayGestureWithoutBuzzerRunsMotionOnly) {
    DigiBiped biped;
    MockChannel ll, rl, lf, rf;
    biped.attachChannels(&ll, &rl, &lf, &rf);
    biped.init();

    // No attachBuzzer call — buzzer pointer is null.
    biped.playGesture(GESTURE_CHEER, /*nowMs=*/0);
    EXPECT_EQ(biped.currentMotion(), DigiBiped::MOTION_GESTURE);
    // No crash on null buzzer path is the contract; motion still runs.
}

TEST(DigiBiped, PlayGestureInvokesAttachedBuzzerWhenSoundBound) {
    DigiBiped biped;
    MockChannel ll, rl, lf, rf;
    biped.attachChannels(&ll, &rl, &lf, &rf);
    biped.init();

    RecordingBuzzer buz;
    buz.attach(25);
    biped.attachBuzzer(&buz);

    biped.playGesture(GESTURE_CONFIRMATION, /*nowMs=*/0);

    // CONFIRMATION binds BEEP_OK_SHORT — exactly one preset dispatch.
    ASSERT_EQ(buz.presets.size(), 1u);
    EXPECT_EQ(buz.presets[0], BEEP_OK_SHORT);
}

TEST(DigiBiped, PlayGestureSilentDoesNotInvokeBuzzer) {
    DigiBiped biped;
    MockChannel ll, rl, lf, rf;
    biped.attachChannels(&ll, &rl, &lf, &rf);
    biped.init();

    RecordingBuzzer buz;
    buz.attach(25);
    biped.attachBuzzer(&buz);

    // SEARCH binds BEEP_NONE — motion runs but buzzer is bypassed.
    biped.playGesture(GESTURE_SEARCH, /*nowMs=*/0);
    EXPECT_TRUE(buz.presets.empty());
    EXPECT_EQ(biped.currentMotion(), DigiBiped::MOTION_GESTURE);
}

TEST(DigiBiped, PlayGestureTickAdvancesTargetsOnAllChannels) {
    DigiBiped biped;
    MockChannel ll, rl, lf, rf;
    biped.attachChannels(&ll, &rl, &lf, &rf);
    biped.init();

    // Reset post-init counters.
    ll.setTargetCalls = rl.setTargetCalls = 0;
    lf.setTargetCalls = rf.setTargetCalls = 0;

    biped.playGesture(GESTURE_YES, /*nowMs=*/0);

    // Drive a few ticks. Each tick → 1 bank.setTargets call → 4 channel
    // setTarget calls.
    biped.tick(100);
    biped.tick(200);
    biped.tick(300);

    EXPECT_EQ(ll.setTargetCalls, 3);
    EXPECT_EQ(rl.setTargetCalls, 3);
    EXPECT_EQ(lf.setTargetCalls, 3);
    EXPECT_EQ(rf.setTargetCalls, 3);
}

TEST(DigiBiped, PlayGestureCompletesAfterRequestedCycles) {
    DigiBiped biped;
    MockChannel ll, rl, lf, rf;
    biped.attachChannels(&ll, &rl, &lf, &rf);
    biped.init();

    // YES gesture: cycles=2, periodMs=800. Motion completes when
    // elapsed >= cycles * periodMs = 1600 ms.
    biped.playGesture(GESTURE_YES, /*nowMs=*/0);
    biped.tick(500);
    biped.tick(1000);
    EXPECT_FALSE(biped.isIdle());

    biped.tick(2000);
    EXPECT_TRUE(biped.isIdle());
    EXPECT_EQ(biped.currentMotion(), DigiBiped::MOTION_IDLE);
    // Motion-complete path returns all channels to HOME_DEG.
    EXPECT_EQ(ll.lastSetTarget, DigiBiped::HOME_DEG);
    EXPECT_EQ(rl.lastSetTarget, DigiBiped::HOME_DEG);
    EXPECT_EQ(lf.lastSetTarget, DigiBiped::HOME_DEG);
    EXPECT_EQ(rf.lastSetTarget, DigiBiped::HOME_DEG);
}

TEST(DigiBiped, PlayGestureNoneSentinelIsFullNoOp) {
    DigiBiped biped;
    MockChannel ll, rl, lf, rf;
    biped.attachChannels(&ll, &rl, &lf, &rf);
    biped.init();

    RecordingBuzzer buz;
    buz.attach(25);
    biped.attachBuzzer(&buz);

    ll.setTargetCalls = rl.setTargetCalls = 0;
    lf.setTargetCalls = rf.setTargetCalls = 0;

    biped.playGesture(GESTURE_NONE, /*nowMs=*/0);

    // No motion transition, no buzzer dispatch.
    EXPECT_TRUE(biped.isIdle());
    EXPECT_EQ(biped.currentMotion(), DigiBiped::MOTION_IDLE);
    EXPECT_TRUE(buz.presets.empty());
    // Channels untouched.
    EXPECT_EQ(ll.setTargetCalls, 0);
    EXPECT_EQ(rl.setTargetCalls, 0);
    EXPECT_EQ(lf.setTargetCalls, 0);
    EXPECT_EQ(rf.setTargetCalls, 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
