#ifndef MOTOR_DRIVE_PROFILE_H
#define MOTOR_DRIVE_PROFILE_H

// Pure production motor drive profile: no Arduino / ESP-IDF dependencies so this header
// can be unit-tested on the host (see test/test_motor_profile).
//
// Hardware context (DRV8871 @ +8.2V, IN/IN interface, High-Speed LEDC PWM @ 1 kHz):
//   IN(drive) = 255, IN(other) = 255 - duty -> slow-decay PWM (off-phase brakes, linear low speed)
//   IN1 = 255, IN2 = 255                    -> active dynamic brake (kills rotor inertia)
//   IN1 = 0,   IN2 = 0                      -> coast / DRV8871 10 uA sleep (worm holds mechanically)
//
// Mechanics: DC motor -> self-locking brass worm -> plastic worm wheel -> pinion -> arm with magnet.
// Opening pushes against the parcel/mail latch mass & spring (heavier on large mailboxes); locking is
// spring-assisted, so locking uses half the breakaway kick duration and a lower stall escalation cap.

#include <stdint.h>

struct MotorOutput {
    int drive;
    int other;
};

namespace MotorProfile {

constexpr int PWM_MAX = 255;               // 8-bit LEDC; 255 maps to 100% solid HIGH in Arduino-ESP32
constexpr int FULL_POWER_DUTY = 255;       // True 100% breakaway kick torque

constexpr int DUTY_MIN = 20;
constexpr int DUTY_MAX = 255;
constexpr int OPEN_DUTY_MIN = 20;
constexpr int OPEN_DUTY_MAX = 255;
constexpr int OPEN_DUTY_DEFAULT = 110;
constexpr int CLOSE_DUTY_MIN = 20;
constexpr int CLOSE_DUTY_MAX = 180;
constexpr int CLOSE_DUTY_DEFAULT = 50;

constexpr int KICK_MS_MIN = 0;
constexpr int KICK_MS_MAX = 100;
constexpr int KICK_MS_DEFAULT = 20;

// Hold active dynamic brake (255, 255) after reaching a limit switch, then enter DRV8871 sleep (0, 0).
constexpr unsigned long BRAKE_HOLD_MS = 200;

// Direction-aware stall escalation: if the target sensor is not reached after ESCALATION_START_MS,
// raise duty step by step so heavy outdoor latches / cold grease reliably finish moving.
// Opening escalates up to full power (255); closing caps lower (180) to avoid slamming the closed stop.
constexpr unsigned long ESCALATION_START_MS = 800;
constexpr unsigned long ESCALATION_STEP_MS = 200;
constexpr int ESCALATION_STEP_DUTY = 25;
constexpr int ESCALATION_MAX_DUTY_OPEN = 255;
constexpr int ESCALATION_MAX_DUTY_CLOSE = 180;

// Stall timeout before entering MOTOR_ERROR.
constexpr unsigned long TIMEOUT_MS = 3200;

inline int clampInt(int value, int lo, int hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

inline MotorOutput brake() { return {PWM_MAX, PWM_MAX}; }
inline MotorOutput coast() { return {0, 0}; }

inline MotorOutput idleOutput(unsigned long idleElapsedMs) {
    return idleElapsedMs < BRAKE_HOLD_MS ? brake() : coast();
}

inline int effectiveKickMs(int kickMs, bool opening) {
    int clamped = clampInt(kickMs, KICK_MS_MIN, KICK_MS_MAX);
    return opening ? clamped : (clamped / 2);
}

inline int escalatedDuty(int baseDuty, unsigned long elapsedMs, bool opening = true) {
    int duty = clampInt(baseDuty, DUTY_MIN, DUTY_MAX);
    if (elapsedMs < ESCALATION_START_MS) {
        return duty;
    }
    int maxCap = opening ? ESCALATION_MAX_DUTY_OPEN : ESCALATION_MAX_DUTY_CLOSE;
    int cap = duty > maxCap ? duty : maxCap;
    unsigned long steps = (elapsedMs - ESCALATION_START_MS) / ESCALATION_STEP_MS + 1;
    unsigned long raised = (unsigned long)duty + steps * ESCALATION_STEP_DUTY;
    return raised > (unsigned long)cap ? cap : (int)raised;
}

// Slow-decay PWM output for one direction (caller maps drive/other onto IN1/IN2).
inline MotorOutput driveOutput(unsigned long elapsedMs, int targetDuty, int kickMs, bool opening = true) {
    const int duty = clampInt(targetDuty, DUTY_MIN, DUTY_MAX);
    const unsigned long kick = (unsigned long)effectiveKickMs(kickMs, opening);

    if (elapsedMs < kick) {
        return {PWM_MAX, PWM_MAX - FULL_POWER_DUTY};
    }
    return {PWM_MAX, PWM_MAX - duty};
}

} // namespace MotorProfile

#endif
