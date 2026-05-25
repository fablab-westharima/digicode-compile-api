// TrimStore host-side unit tests (Phase A-ε commit 1).
//
// Verifies:
//   - default state (all zero, count 0)
//   - set/get round-trip + out-of-range pin guard
//   - range clamping at TRIM_MAX / TRIM_MIN
//   - clearAll zeroes everything
//   - applyToChannel pushes the stored value via
//     IActuatorChannel::setTrim() (null + out-of-range safe)
//   - save/load round-trip via a MockNvsBackend (simulating Preferences)
//   - Session 143 lifecycle (c) "両方併用、後勝ち" precedence pin:
//     boot-time applyToChannel followed by runtime set() + applyToChannel
//     yields the runtime value on the channel.
//
// MockChannel implements the IActuatorChannel surface with deterministic
// lastTrim observation. MockNvsBackend replaces the ESP32 Preferences
// API with a std::map<string,int>. TestTrimStore subclasses
// PortableTrimStore to route load/save through the mock backend,
// mirroring the schema in NvsTrimStore_esp32.cpp (key "t<pin>").
//
// 8 cases. Layer 4 platform-agnostic: no Arduino.h / ESP32 API in this
// file or in the source it tests.

#include <gtest/gtest.h>

#include <cstdio>
#include <map>
#include <string>

#include "trim/ITrimStore.h"

namespace {

class MockChannel : public IActuatorChannel {
public:
    int lastTrim = -999;
    int setTrimCalls = 0;

    bool attach() override { return true; }
    void detach() override {}
    bool isAttached() const override { return true; }

    void setTarget(long) override {}
    long getTarget() const override { return 0; }
    long getCurrent() const override { return 0; }
    bool hasReachedTarget() const override { return true; }

    void setPulseRange(int, int) override {}
    void setMaxRate(int) override {}
    void setTrim(int v) override { lastTrim = v; ++setTrimCalls; }
    void setReverse(bool) override {}

    int getPulseMin() const override { return 0; }
    int getPulseMax() const override { return 0; }
    int getMaxRate() const override  { return 0; }
    int getTrim() const override     { return lastTrim; }
    bool getReverse() const override { return false; }
    int getLastWrittenHw() const override { return 0; }

    void pump(unsigned long) override {}
    bool isActive() const override { return false; }
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

// TestTrimStore mirrors NvsTrimStore_esp32.cpp's schema (key "t<pin>")
// but writes to a MockNvsBackend instead of Preferences. Lets host
// tests verify save/load round-trip without an ESP32 in the loop.
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
            int v = _backend.getInt(key, 0);
            if (v < TRIM_MIN) v = TRIM_MIN;
            if (v > TRIM_MAX) v = TRIM_MAX;
            _trims[pin] = v;
        }
    }

private:
    MockNvsBackend& _backend;
};

}  // namespace

TEST(TrimStore, DefaultConstructionAllZeroNoTrim) {
    PortableTrimStore store;
    for (int pin = 0; pin < PortableTrimStore::MAX_PIN; ++pin) {
        EXPECT_EQ(store.get(pin), 0);
    }
    EXPECT_EQ(store.count(), 0);
}

TEST(TrimStore, SetTrimReadbackByPin) {
    PortableTrimStore store;
    store.set(27, 5);
    EXPECT_EQ(store.get(27), 5);
    store.set(15, -8);
    EXPECT_EQ(store.get(15), -8);
    EXPECT_EQ(store.count(), 2);

    // Out-of-range pin silently ignored on both ends.
    store.set(-1, 10);
    store.set(PortableTrimStore::MAX_PIN, 10);
    EXPECT_EQ(store.get(-1), 0);
    EXPECT_EQ(store.get(PortableTrimStore::MAX_PIN), 0);
    EXPECT_EQ(store.count(), 2);
}

TEST(TrimStore, SetTrimClampsToMax) {
    PortableTrimStore store;
    store.set(27, 50);
    EXPECT_EQ(store.get(27), PortableTrimStore::TRIM_MAX);
}

TEST(TrimStore, SetTrimClampsToMin) {
    PortableTrimStore store;
    store.set(27, -50);
    EXPECT_EQ(store.get(27), PortableTrimStore::TRIM_MIN);
}

TEST(TrimStore, ClearAllZeroesEntries) {
    PortableTrimStore store;
    store.set(27, 5);
    store.set(15, -8);
    store.set(14, 12);
    EXPECT_EQ(store.count(), 3);

    store.clearAll();
    EXPECT_EQ(store.count(), 0);
    EXPECT_EQ(store.get(27), 0);
    EXPECT_EQ(store.get(15), 0);
    EXPECT_EQ(store.get(14), 0);
}

TEST(TrimStore, ApplyToChannelInvokesSetTrim) {
    PortableTrimStore store;
    MockChannel ch;

    store.set(27, 7);
    store.applyToChannel(&ch, 27);
    EXPECT_EQ(ch.lastTrim, 7);
    EXPECT_EQ(ch.setTrimCalls, 1);

    // Null channel: silent no-op, no crash, no side effect.
    store.applyToChannel(nullptr, 27);
    EXPECT_EQ(ch.setTrimCalls, 1);

    // Out-of-range pin: silent no-op.
    store.applyToChannel(&ch, -1);
    EXPECT_EQ(ch.setTrimCalls, 1);
    store.applyToChannel(&ch, PortableTrimStore::MAX_PIN);
    EXPECT_EQ(ch.setTrimCalls, 1);

    // Unset pin: applies 0 (default, intentional — boot-time iteration
    // over all channels should leave un-trimmed pins at zero offset).
    store.applyToChannel(&ch, 0);
    EXPECT_EQ(ch.lastTrim, 0);
    EXPECT_EQ(ch.setTrimCalls, 2);
}

TEST(TrimStore, SaveLoadRoundtripPreservesValues) {
    MockNvsBackend backend;

    // Session A: set + save
    {
        TestTrimStore store(backend);
        store.set(27, 5);
        store.set(15, -8);
        store.set(14, 12);
        store.set(13, -3);
        store.save();
    }

    // Session B: fresh instance + load + verify
    TestTrimStore store2(backend);
    EXPECT_EQ(store2.count(), 0);  // fresh, not yet loaded
    store2.load();
    EXPECT_EQ(store2.get(27), 5);
    EXPECT_EQ(store2.get(15), -8);
    EXPECT_EQ(store2.get(14), 12);
    EXPECT_EQ(store2.get(13), -3);
    EXPECT_EQ(store2.count(), 4);
}

// Session 143 lifecycle (c) "両方併用、後勝ち" precedence pin.
// Documents and verifies the contract that boot-time applyToChannel
// followed by a runtime set() + applyToChannel yields the runtime
// value. The store holds no implicit precedence rule — precedence is
// the responsibility of the call sequence (Layer 5 robot init at
// startup vs ServoTrimDialog command at runtime via Phase D
// transport).
TEST(TrimStore, RuntimeOverrideAfterBootLastWriteWins) {
    MockNvsBackend backend;
    MockChannel ch;

    // --- Boot phase: previous session had pin 27 trimmed to +5° ---
    backend.putInt("t27", 5);

    TestTrimStore store(backend);
    store.load();
    store.applyToChannel(&ch, 27);  // boot-time push
    EXPECT_EQ(ch.lastTrim, 5);
    EXPECT_EQ(ch.setTrimCalls, 1);

    // --- Runtime phase: ServoTrimDialog sends a new value ---
    store.set(27, -10);
    store.applyToChannel(&ch, 27);  // runtime push
    EXPECT_EQ(ch.lastTrim, -10);    // runtime overrides boot value
    EXPECT_EQ(ch.setTrimCalls, 2);

    // Store's in-memory state matches the latest set().
    EXPECT_EQ(store.get(27), -10);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
