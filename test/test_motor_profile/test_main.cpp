#include <unity.h>
#include "MotorDriveProfile.h"

using namespace MotorProfile;

void setUp(void) {}
void tearDown(void) {}

static void assertOutput(int expectedDrive, int expectedOther, MotorOutput actual) {
    TEST_ASSERT_EQUAL_INT(expectedDrive, actual.drive);
    TEST_ASSERT_EQUAL_INT(expectedOther, actual.other);
}

void test_slow_decay_kick_then_drive_brake_pwm(void) {
    // Full-power breakaway kick (255 -> {255, 0}) for the first kickMs ms
    assertOutput(PWM_MAX, PWM_MAX - FULL_POWER_DUTY, driveOutput(0, 110, 20, true));
    assertOutput(PWM_MAX, PWM_MAX - FULL_POWER_DUTY, driveOutput(19, 110, 20, true));
    // Then steady slow-decay: drive pin stays HIGH, other pin gets 255 - duty
    assertOutput(PWM_MAX, PWM_MAX - 110, driveOutput(20, 110, 20, true));
    assertOutput(PWM_MAX, PWM_MAX - 110, driveOutput(500, 110, 20, true));
}

void test_slow_decay_without_kick(void) {
    assertOutput(PWM_MAX, PWM_MAX - 80, driveOutput(0, 80, 0, true));
}

void test_slow_decay_supports_full_power_opening(void) {
    assertOutput(PWM_MAX, 0, driveOutput(50, 255, 20, true));
}

void test_drive_output_clamps_duty_and_kick(void) {
    assertOutput(PWM_MAX, PWM_MAX - DUTY_MIN, driveOutput(100, 0, 0, true));
    assertOutput(PWM_MAX, PWM_MAX - DUTY_MAX, driveOutput(100, 999, 0, true));
    assertOutput(PWM_MAX, PWM_MAX - FULL_POWER_DUTY, driveOutput(KICK_MS_MAX - 1, 80, 999, true));
    assertOutput(PWM_MAX, PWM_MAX - 80, driveOutput(KICK_MS_MAX, 80, 999, true));
}

void test_closing_uses_reduced_kick_duration(void) {
    // Opening gets full 20ms kick; spring-assisted closing gets half (10ms)
    TEST_ASSERT_EQUAL_INT(20, effectiveKickMs(20, true));
    TEST_ASSERT_EQUAL_INT(10, effectiveKickMs(20, false));
    assertOutput(PWM_MAX, PWM_MAX - FULL_POWER_DUTY, driveOutput(9, 50, 20, false));
    assertOutput(PWM_MAX, PWM_MAX - 50, driveOutput(10, 50, 20, false));
}

void test_idle_output_brakes_then_releases_to_coast(void) {
    assertOutput(PWM_MAX, PWM_MAX, idleOutput(0));
    assertOutput(PWM_MAX, PWM_MAX, idleOutput(BRAKE_HOLD_MS - 1));
    assertOutput(0, 0, idleOutput(BRAKE_HOLD_MS));
    assertOutput(0, 0, idleOutput(5000));
}

void test_escalation_inactive_before_start(void) {
    TEST_ASSERT_EQUAL_INT(110, escalatedDuty(110, 0, true));
    TEST_ASSERT_EQUAL_INT(110, escalatedDuty(110, ESCALATION_START_MS - 1, true));
}

void test_opening_escalation_reaches_full_power_255(void) {
    // Even starting at 155 (large mailbox), opening escalates step by step all the way to 255
    TEST_ASSERT_EQUAL_INT(180, escalatedDuty(155, ESCALATION_START_MS, true));
    TEST_ASSERT_EQUAL_INT(205, escalatedDuty(155, ESCALATION_START_MS + ESCALATION_STEP_MS, true));
    TEST_ASSERT_EQUAL_INT(230, escalatedDuty(155, ESCALATION_START_MS + 2 * ESCALATION_STEP_MS, true));
    TEST_ASSERT_EQUAL_INT(255, escalatedDuty(155, ESCALATION_START_MS + 3 * ESCALATION_STEP_MS, true));
    TEST_ASSERT_EQUAL_INT(ESCALATION_MAX_DUTY_OPEN, escalatedDuty(DUTY_MIN, TIMEOUT_MS - 1, true));
}

void test_closing_escalation_caps_below_full_power(void) {
    // Closing caps at ESCALATION_MAX_DUTY_CLOSE (180) so it does not slam into the closed stop
    TEST_ASSERT_EQUAL_INT(75, escalatedDuty(50, ESCALATION_START_MS, false));
    TEST_ASSERT_EQUAL_INT(ESCALATION_MAX_DUTY_CLOSE, escalatedDuty(50, TIMEOUT_MS - 1, false));
    // If configured base duty already exceeds close cap, keep base duty (never lower it)
    TEST_ASSERT_EQUAL_INT(200, escalatedDuty(200, TIMEOUT_MS - 1, false));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_slow_decay_kick_then_drive_brake_pwm);
    RUN_TEST(test_slow_decay_without_kick);
    RUN_TEST(test_slow_decay_supports_full_power_opening);
    RUN_TEST(test_drive_output_clamps_duty_and_kick);
    RUN_TEST(test_closing_uses_reduced_kick_duration);
    RUN_TEST(test_idle_output_brakes_then_releases_to_coast);
    RUN_TEST(test_escalation_inactive_before_start);
    RUN_TEST(test_opening_escalation_reaches_full_power_255);
    RUN_TEST(test_closing_escalation_caps_below_full_power);
    return UNITY_END();
}
