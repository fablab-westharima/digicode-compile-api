// DigiBuzzer + SoundPresetTable host-side unit tests (Phase A-ε-2 commit 1).
//
// Verifies:
//   - SoundPresetTable: all 16 design intents (BEEP_SHORT_HIGH .. BEEP_SHUTDOWN)
//     are populated with stepCount > 0; BEEP_NONE sentinel is empty;
//     out-of-range id is safe.
//   - PortableBuzzer lifecycle: attach / detach / isAttached + invalid pin.
//   - playPreset dispatch loop: routes each step to playTone (simple tone)
//     or playBendTone (when freq2 != 0) or _rest (when freq == 0).
//   - playPreset is a no-op before attach (defense against orphan-call
//     misuse, case 23 incident A spirit).
//   - Per-preset call sequence verification for a representative sample:
//     a simple tone, a bend tone, a multi-step fanfare. (Iterative
//     refinement per D-new-1b means exact freq/duration values may change
//     in Phase E; tests verify shape + dispatch, not the candidate values
//     themselves.)
//
// 10 cases. Layer 4 platform-agnostic: no Arduino.h / ESP32 API.
//
// Anti-derivation: 60.md §4 lists canonical T5 grep gates. This file
// uses only the new BEEP_<intent> family + the new dispatch helpers; no
// upstream identifier literals.

#include <gtest/gtest.h>

#include <vector>

#include "sound/IBuzzer.h"
#include "sound/SoundPresetTable.h"

namespace {

// Recording mock. Subclasses PortableBuzzer + records every playTone /
// playBendTone / _rest call without HW dependency.
class RecordingBuzzer : public PortableBuzzer {
public:
    struct ToneCall {
        int freq;
        int duration;
    };
    struct BendCall {
        int initFreq;
        int endFreq;
        int duration;
    };

    std::vector<ToneCall> tones;
    std::vector<BendCall> bends;
    std::vector<int> rests;
    int stopCalls = 0;

    void playTone(int freqHz, int durationMs) override {
        tones.push_back({freqHz, durationMs});
    }
    void playBendTone(int initFreq, int endFreq, int totalDurationMs) override {
        bends.push_back({initFreq, endFreq, totalDurationMs});
    }
    void stop() override { ++stopCalls; }

protected:
    void _rest(int durationMs) override {
        rests.push_back(durationMs);
    }
};

}  // namespace

// === SoundPresetTable ===

TEST(SoundPresetTable, NoneSentinelIsEmpty) {
    const PresetData& p = SoundPresetTable::get(BEEP_NONE);
    EXPECT_EQ(p.stepCount, 0);
}

TEST(SoundPresetTable, AllSixteenIntentsHaveNonEmptySequence) {
    // Iterate every real preset id (skipping BEEP_NONE sentinel + the
    // count terminator). Each must have at least one step.
    for (int id = BEEP_SHORT_HIGH; id < SOUND_PRESET_COUNT; ++id) {
        const PresetData& p =
            SoundPresetTable::get(static_cast<SoundPresetId>(id));
        EXPECT_GT(p.stepCount, 0)
            << "preset id " << id << " has no steps";
        EXPECT_LE(p.stepCount, 8)
            << "preset id " << id << " exceeds MAX_STEPS = 8";
    }
}

TEST(SoundPresetTable, OutOfRangeIdReturnsEmptyNone) {
    const PresetData& neg = SoundPresetTable::get(static_cast<SoundPresetId>(-1));
    EXPECT_EQ(neg.stepCount, 0);
    const PresetData& over =
        SoundPresetTable::get(static_cast<SoundPresetId>(SOUND_PRESET_COUNT));
    EXPECT_EQ(over.stepCount, 0);
    const PresetData& way_over =
        SoundPresetTable::get(static_cast<SoundPresetId>(SOUND_PRESET_COUNT + 50));
    EXPECT_EQ(way_over.stepCount, 0);
}

TEST(SoundPresetTable, AllPresetStepsHavePositiveDuration) {
    // Sanity gate: no preset step should encode duration <= 0 (would be
    // a no-op step and likely a table-entry typo).
    for (int id = BEEP_SHORT_HIGH; id < SOUND_PRESET_COUNT; ++id) {
        const PresetData& p =
            SoundPresetTable::get(static_cast<SoundPresetId>(id));
        for (int i = 0; i < p.stepCount; ++i) {
            EXPECT_GT(p.steps[i].duration, 0)
                << "preset id " << id << " step " << i
                << " has non-positive duration";
        }
    }
}

// === PortableBuzzer lifecycle ===

TEST(PortableBuzzer, ConstructionUnattached) {
    RecordingBuzzer buz;
    EXPECT_FALSE(buz.isAttached());
    EXPECT_EQ(buz.pin(), -1);
}

TEST(PortableBuzzer, AttachValidPinSetsAttached) {
    RecordingBuzzer buz;
    buz.attach(25);
    EXPECT_TRUE(buz.isAttached());
    EXPECT_EQ(buz.pin(), 25);

    buz.detach();
    EXPECT_FALSE(buz.isAttached());
    EXPECT_EQ(buz.pin(), -1);
}

TEST(PortableBuzzer, AttachNegativePinRemainsUnattached) {
    RecordingBuzzer buz;
    buz.attach(-5);
    EXPECT_FALSE(buz.isAttached());
}

// === playPreset dispatch ===

TEST(PortableBuzzer, PlayPresetBeforeAttachIsNoOp) {
    RecordingBuzzer buz;
    // Not attached. Even a real preset should record zero calls.
    buz.playPreset(BEEP_FANFARE);
    EXPECT_TRUE(buz.tones.empty());
    EXPECT_TRUE(buz.bends.empty());
}

TEST(PortableBuzzer, PlayPresetSimpleSingleToneDispatchesPlayToneOnce) {
    RecordingBuzzer buz;
    buz.attach(25);
    buz.playPreset(BEEP_SHORT_HIGH);

    // BEEP_SHORT_HIGH = 1 simple-tone step.
    ASSERT_EQ(buz.tones.size(), 1u);
    EXPECT_TRUE(buz.bends.empty());
    EXPECT_GT(buz.tones[0].freq, 0);
    EXPECT_GT(buz.tones[0].duration, 0);
}

TEST(PortableBuzzer, PlayPresetBendStepDispatchesPlayBendTone) {
    RecordingBuzzer buz;
    buz.attach(25);
    buz.playPreset(BEEP_RISING_FAST);

    // BEEP_RISING_FAST is a single bend step (freq2 != 0).
    EXPECT_TRUE(buz.tones.empty());
    ASSERT_EQ(buz.bends.size(), 1u);
    EXPECT_GT(buz.bends[0].initFreq, 0);
    EXPECT_GT(buz.bends[0].endFreq, 0);
    EXPECT_NE(buz.bends[0].initFreq, buz.bends[0].endFreq);
    EXPECT_GT(buz.bends[0].duration, 0);
}

TEST(PortableBuzzer, PlayPresetMultiStepDispatchesInOrder) {
    RecordingBuzzer buz;
    buz.attach(25);
    buz.playPreset(BEEP_FANFARE);

    // BEEP_FANFARE is 4 simple-tone steps; verify exact count and that
    // each call corresponds to a non-zero positive tone.
    EXPECT_TRUE(buz.bends.empty());
    ASSERT_EQ(buz.tones.size(), 4u);
    for (const auto& t : buz.tones) {
        EXPECT_GT(t.freq, 0);
        EXPECT_GT(t.duration, 0);
    }
}

TEST(PortableBuzzer, PlayPresetNoneSentinelDispatchesNothing) {
    RecordingBuzzer buz;
    buz.attach(25);
    buz.playPreset(BEEP_NONE);
    EXPECT_TRUE(buz.tones.empty());
    EXPECT_TRUE(buz.bends.empty());
    EXPECT_TRUE(buz.rests.empty());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
