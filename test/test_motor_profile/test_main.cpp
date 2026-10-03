#include <unity.h>
#include "MotorDriveProfile.h"

using namespace MotorProfile;

void setUp(void) {}
void tearDown(void) {}

static void assertOutput(int expectedDrive, int expectedOther, MotorOutput out) {
    TEST_ASSERT_EQUAL_INT(expectedDrive, out.drive);
    TEST_ASSERT_EQUAL_INT(expectedOther, out.other);
}

// --- Classic profile must be identical to the original firmware (origin/main) ---

void test_classic_matches_original_firmware(void) {
    assertOutput(200, 0, driveOutput(DRIVE_MODE_CLASSIC, 0, 120, 10));
    assertOutput(200, 0, driveOutput(DRIVE_MODE_CLASSIC, 99, 120, 10));
    assertOutput(160, 0, driveOutput(DRIVE_MODE_CLASSIC, 105, 120, 10)); // 200 - 80*5/10
    assertOutput(120, 0, driveOutput(DRIVE_MODE_CLASSIC, 110, 120, 10));
    assertOutput(120, 0, driveOutput(DRIVE_MODE_CLASSIC, 1500, 120, 10));
    assertOutput(20, 0, driveOutput(DRIVE_MODE_CLASSIC, 1500, 20, 10)); // original close default
}

void test_invalid_mode_falls_back_to_classic(void) {
    assertOutput(200, 0, driveOutput(7, 50, 60, 10));
    assertOutput(200, 0, driveOutput(-1, 50, 60, 10));
    TEST_ASSERT_EQUAL_INT(DRIVE_MODE_CLASSIC, sanitizeMode(42));
}

// --- Slow decay ---

void test_slow_decay_kick_then_drive_brake_pwm(void) {
    assertOutput(255, 55, driveOutput(DRIVE_MODE_SLOW_DECAY, 0, 60, 10));  // kick = 200/255 drive
    assertOutput(255, 55, driveOutput(DRIVE_MODE_SLOW_DECAY, 9, 60, 10));
    assertOutput(255, 195, driveOutput(DRIVE_MODE_SLOW_DECAY, 10, 60, 10)); // 60/255 drive
    assertOutput(255, 195, driveOutput(DRIVE_MODE_SLOW_DECAY, 3000, 60, 10));
}

void test_slow_decay_without_kick(void) {
    assertOutput(255, 195, driveOutput(DRIVE_MODE_SLOW_DECAY, 0, 60, 0));
}

void test_slow_decay_never_exceeds_kick_power_after_kick(void) {
    // No full-power phase after the kick -> no overshoot-prone 100 ms boost as in classic mode.
    for (unsigned long t = 10; t < 200; t++) {
        MotorOutput out = driveOutput(DRIVE_MODE_SLOW_DECAY, t, 40, 10);
        TEST_ASSERT_EQUAL_INT(255 - 40, out.other);
    }
}

void test_drive_output_clamps_duty_and_kick(void) {
    assertOutput(255, 235, driveOutput(DRIVE_MODE_SLOW_DECAY, 100, 5, 10));    // duty -> 20
    assertOutput(255, 95, driveOutput(DRIVE_MODE_SLOW_DECAY, 100, 999, 10));   // duty -> 160
    assertOutput(255, 195, driveOutput(DRIVE_MODE_SLOW_DECAY, 40, 60, 1000));  // kick -> 40 ms
    assertOutput(255, 195, driveOutput(DRIVE_MODE_SLOW_DECAY, 0, 60, -5));     // kick -> 0 ms
}

// --- Inching ---

void test_inching_starts_with_pause_after_kick(void) {
    // Regression: the old code started the cycle with a pulse, giving ~30 ms of continuous drive.
    assertOutput(200, 0, driveOutput(DRIVE_MODE_INCHING, 9, 60, 10));
    assertOutput(255, 255, driveOutput(DRIVE_MODE_INCHING, 10, 60, 10)); // brake
}

void test_inching_first_30ms_drive_only_during_kick(void) {
    int drivenMs = 0;
    for (unsigned long t = 0; t < 30; t++) {
        MotorOutput out = driveOutput(DRIVE_MODE_INCHING, t, 60, 10);
        if (out.drive > 0 && out.other == 0) drivenMs++;
    }
    TEST_ASSERT_EQUAL_INT(10, drivenMs);
}

void test_inching_cycle_timing_default_duty(void) {
    // pause = (140-60)*80/120 = 53 ms, pulse 15 ms, cycle 68 ms
    TEST_ASSERT_EQUAL_INT(53, inchingPauseMs(60));
    assertOutput(255, 255, driveOutput(DRIVE_MODE_INCHING, 10 + 52, 60, 10));
    assertOutput(120, 0, driveOutput(DRIVE_MODE_INCHING, 10 + 53, 60, 10));
    assertOutput(120, 0, driveOutput(DRIVE_MODE_INCHING, 10 + 67, 60, 10));
    assertOutput(255, 255, driveOutput(DRIVE_MODE_INCHING, 10 + 68, 60, 10));
}

void test_inching_pause_range_and_monotonic(void) {
    TEST_ASSERT_EQUAL_INT(80, inchingPauseMs(20));
    TEST_ASSERT_EQUAL_INT(80, inchingPauseMs(5));
    TEST_ASSERT_EQUAL_INT(0, inchingPauseMs(140));
    TEST_ASSERT_EQUAL_INT(0, inchingPauseMs(160));
    for (int duty = 21; duty <= 160; duty++) {
        TEST_ASSERT_TRUE(inchingPauseMs(duty) <= inchingPauseMs(duty - 1));
    }
}

void test_inching_continuous_at_or_above_threshold(void) {
    assertOutput(140, 0, driveOutput(DRIVE_MODE_INCHING, 50, 140, 10));
    assertOutput(160, 0, driveOutput(DRIVE_MODE_INCHING, 500, 160, 10));
}

// --- Stall escalation ---

void test_escalation_not_applied_in_classic_mode(void) {
    TEST_ASSERT_EQUAL_INT(20, escalatedDuty(DRIVE_MODE_CLASSIC, 20, 1999));
}

void test_escalation_inactive_before_start(void) {
    TEST_ASSERT_EQUAL_INT(30, escalatedDuty(DRIVE_MODE_SLOW_DECAY, 30, 0));
    TEST_ASSERT_EQUAL_INT(30, escalatedDuty(DRIVE_MODE_SLOW_DECAY, 30, 1499));
}

void test_escalation_steps_up_to_max(void) {
    TEST_ASSERT_EQUAL_INT(40, escalatedDuty(DRIVE_MODE_SLOW_DECAY, 20, 1500));
    TEST_ASSERT_EQUAL_INT(40, escalatedDuty(DRIVE_MODE_SLOW_DECAY, 20, 1749));
    TEST_ASSERT_EQUAL_INT(60, escalatedDuty(DRIVE_MODE_SLOW_DECAY, 20, 1750));
    TEST_ASSERT_EQUAL_INT(160, escalatedDuty(DRIVE_MODE_INCHING, 20, 3250));
    TEST_ASSERT_EQUAL_INT(160, escalatedDuty(DRIVE_MODE_INCHING, 20, 100000));
}

void test_escalation_reaches_max_well_before_timeout(void) {
    // From the lowest duty, full escalation must leave at least 1 s at max duty before MOTOR_ERROR.
    unsigned long t = ESCALATION_START_MS;
    while (escalatedDuty(DRIVE_MODE_SLOW_DECAY, DUTY_MIN, t) < DUTY_MAX) t++;
    TEST_ASSERT_TRUE(t + 1000 <= SLOW_TIMEOUT_MS);
}

// --- Timeouts and calibration support ---

void test_timeouts(void) {
    TEST_ASSERT_EQUAL_UINT32(2000, timeoutMs(DRIVE_MODE_CLASSIC));
    TEST_ASSERT_EQUAL_UINT32(4500, timeoutMs(DRIVE_MODE_SLOW_DECAY));
    TEST_ASSERT_EQUAL_UINT32(4500, timeoutMs(DRIVE_MODE_INCHING));
}

void test_calibration_support(void) {
    TEST_ASSERT_TRUE(calibrationSupported(DRIVE_MODE_CLASSIC));
    TEST_ASSERT_TRUE(calibrationSupported(DRIVE_MODE_SLOW_DECAY));
    TEST_ASSERT_FALSE(calibrationSupported(DRIVE_MODE_INCHING));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_classic_matches_original_firmware);
    RUN_TEST(test_invalid_mode_falls_back_to_classic);
    RUN_TEST(test_slow_decay_kick_then_drive_brake_pwm);
    RUN_TEST(test_slow_decay_without_kick);
    RUN_TEST(test_slow_decay_never_exceeds_kick_power_after_kick);
    RUN_TEST(test_drive_output_clamps_duty_and_kick);
    RUN_TEST(test_inching_starts_with_pause_after_kick);
    RUN_TEST(test_inching_first_30ms_drive_only_during_kick);
    RUN_TEST(test_inching_cycle_timing_default_duty);
    RUN_TEST(test_inching_pause_range_and_monotonic);
    RUN_TEST(test_inching_continuous_at_or_above_threshold);
    RUN_TEST(test_escalation_not_applied_in_classic_mode);
    RUN_TEST(test_escalation_inactive_before_start);
    RUN_TEST(test_escalation_steps_up_to_max);
    RUN_TEST(test_escalation_reaches_max_well_before_timeout);
    RUN_TEST(test_timeouts);
    RUN_TEST(test_calibration_support);
    return UNITY_END();
}
