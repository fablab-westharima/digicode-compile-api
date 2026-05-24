// DigiMotion Phase A-beta skeleton: GoogleTest infrastructure smoke test.
//
// This file verifies that the PIO native + GoogleTest test infrastructure
// is wired up correctly. Real subsystem tests (test_pump, test_servo_channel,
// test_channel_bank, test_sin_oscillator, test_linear_interpolator,
// test_trim_store, test_biped, test_morpher, test_rover, test_buzzer,
// test_gesture_library) land in Phase A-gamma onward per
// plans/active/60_robotics-redesign-implementation-plan.md Section 5.1.

#include <gtest/gtest.h>

TEST(HelloTest, BasicAssertion) {
    EXPECT_EQ(1 + 1, 2);
}

TEST(HelloTest, StringEquality) {
    EXPECT_STREQ("DigiMotion", "DigiMotion");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
