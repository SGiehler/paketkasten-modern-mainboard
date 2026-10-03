#ifndef MOTOR_DRIVE_PROFILE_H
#define MOTOR_DRIVE_PROFILE_H

// Pure motor drive profiles: no Arduino / ESP-IDF dependencies so this header can be
// unit-tested on the host (see test/test_motor_profile).
//
// Hardware context (DRV8871, IN/IN interface, driven by two LEDC PWM channels @ 1 kHz):
//   IN1/IN2 = PWM/0   -> drive, off-phase = coast  ("fast decay")
//   IN1/IN2 = 1/PWM   -> drive, off-phase = brake  ("slow decay", current keeps flowing)
//   IN1/IN2 = 1/1     -> brake
//   IN1/IN2 = 0/0     -> coast
// Mechanics: DC motor -> brass worm -> plastic worm wheel -> pinion -> large gear with arm.
// The worm stage has high static friction (stiction) and cannot be back-driven by the arm.
// Fast runs overshoot the hall sensors in both directions, so opening AND locking use the
// selected (slow) mode.

#include <stdint.h>

enum MotorDriveMode {
    // Original firmware behaviour: 100 ms full power, 10 ms ramp, fast decay.
    // The stroke finishes inside the boost, so this is effectively full speed.
    DRIVE_MODE_CLASSIC = 0,
    // Short breakaway kick, then continuous slow-decay PWM at the slider duty.
    DRIVE_MODE_SLOW_DECAY = 1,
    // Short breakaway kick, then brake pauses alternating with fixed-torque pulses.
    DRIVE_MODE_INCHING = 2,
};

// PWM duties for the two H-bridge inputs, relative to the active direction.
// drive = input of the active direction (IN1 when opening, IN2 when locking), other = opposite input.
struct MotorOutput {
    int drive;
    int other;
};

namespace MotorProfile {

constexpr int PWM_MAX = 255;               // 8-bit LEDC; 255 is mapped to 100 % by the Arduino core
constexpr int FULL_POWER_DUTY = 200;

constexpr int CLASSIC_FULL_POWER_MS = 100; // values of the original firmware (origin/main)
constexpr int CLASSIC_RAMP_DOWN_MS = 10;

constexpr int DUTY_MIN = 20;               // generic bounds for any drive duty
constexpr int DUTY_MAX = 160;
constexpr int OPEN_DUTY_MIN = 20;          // slider bounds
constexpr int OPEN_DUTY_MAX = 160;
constexpr int OPEN_DUTY_DEFAULT = 120;     // original default
constexpr int CLOSE_DUTY_MIN = 20;
constexpr int CLOSE_DUTY_MAX = 100;
constexpr int CLOSE_DUTY_DEFAULT = 20;     // original default

constexpr int KICK_MS_MIN = 0;
constexpr int KICK_MS_MAX = 40;
constexpr int KICK_MS_DEFAULT = 10;

constexpr int INCHING_PULSE_MS = 15;
constexpr int INCHING_PULSE_DUTY = 120;
constexpr int INCHING_MAX_PAUSE_MS = 80;
constexpr int INCHING_THRESHOLD_DUTY = 140; // at/above this the motor runs continuously

// Stall escalation (slow modes only): if the target is not reached after ESCALATION_START_MS,
// raise the duty step by step so a cold/stiff mechanism still reaches its end position instead
// of timing out (a failed LOCKING would leave the mailbox unlocked).
constexpr unsigned long ESCALATION_START_MS = 1500;
constexpr unsigned long ESCALATION_STEP_MS = 250;
constexpr int ESCALATION_STEP_DUTY = 20;   // 20 -> 160 within 1750 ms

constexpr int DEFAULT_MODE = DRIVE_MODE_CLASSIC; // proven behaviour until slow modes are validated on hardware

// Stall timeouts. Slow modes legitimately take longer and include the escalation window.
constexpr unsigned long CLASSIC_TIMEOUT_MS = 2000;
constexpr unsigned long SLOW_TIMEOUT_MS = 4500;

inline int clampInt(int value, int lo, int hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

inline int sanitizeMode(int mode) {
    return (mode == DRIVE_MODE_SLOW_DECAY || mode == DRIVE_MODE_INCHING) ? mode : DRIVE_MODE_CLASSIC;
}

inline MotorOutput brake() { return {PWM_MAX, PWM_MAX}; }

// Original profile: full power boost, linear ramp down, then target duty (fast decay).
inline MotorOutput classicOutput(unsigned long elapsedMs, int targetDuty) {
    if (elapsedMs < (unsigned long)CLASSIC_FULL_POWER_MS) {
        return {FULL_POWER_DUTY, 0};
    }
    if (elapsedMs < (unsigned long)(CLASSIC_FULL_POWER_MS + CLASSIC_RAMP_DOWN_MS)) {
        int rampElapsed = (int)(elapsedMs - CLASSIC_FULL_POWER_MS);
        return {FULL_POWER_DUTY - (FULL_POWER_DUTY - targetDuty) * rampElapsed / CLASSIC_RAMP_DOWN_MS, 0};
    }
    return {targetDuty, 0};
}

// Pause between inching pulses: longest at the minimum duty, 0 at the threshold.
inline int inchingPauseMs(int targetDuty) {
    int duty = clampInt(targetDuty, DUTY_MIN, INCHING_THRESHOLD_DUTY);
    return (INCHING_THRESHOLD_DUTY - duty) * INCHING_MAX_PAUSE_MS / (INCHING_THRESHOLD_DUTY - DUTY_MIN);
}

// Duty after stall escalation. Classic mode is left untouched (original behaviour).
inline int escalatedDuty(int mode, int baseDuty, unsigned long elapsedMs) {
    int duty = clampInt(baseDuty, DUTY_MIN, DUTY_MAX);
    if (sanitizeMode(mode) == DRIVE_MODE_CLASSIC || elapsedMs < ESCALATION_START_MS) {
        return duty;
    }
    unsigned long steps = (elapsedMs - ESCALATION_START_MS) / ESCALATION_STEP_MS + 1;
    unsigned long raised = (unsigned long)duty + steps * ESCALATION_STEP_DUTY;
    return raised > (unsigned long)DUTY_MAX ? DUTY_MAX : (int)raised;
}

// Output for one direction (the caller maps drive/other onto IN1/IN2).
inline MotorOutput driveOutput(int mode, unsigned long elapsedMs, int targetDuty, int kickMs) {
    const int duty = clampInt(targetDuty, DUTY_MIN, DUTY_MAX);
    const unsigned long kick = (unsigned long)clampInt(kickMs, KICK_MS_MIN, KICK_MS_MAX);

    switch (sanitizeMode(mode)) {
        case DRIVE_MODE_SLOW_DECAY:
            // Drive input held high, opposite input PWM: off-phases brake instead of coast,
            // so the current does not collapse at low duty (linear low-speed behaviour).
            if (elapsedMs < kick) {
                return {PWM_MAX, PWM_MAX - FULL_POWER_DUTY};
            }
            return {PWM_MAX, PWM_MAX - duty};

        case DRIVE_MODE_INCHING: {
            if (elapsedMs < kick) {
                return {FULL_POWER_DUTY, 0};
            }
            if (duty >= INCHING_THRESHOLD_DUTY) {
                return {duty, 0};
            }
            // The cycle starts with a PAUSE: the kick alone already moves the arm, so an immediate
            // pulse would turn the first ~30 ms into continuous drive and finish the parcel stroke.
            const unsigned long pause = (unsigned long)inchingPauseMs(duty);
            const unsigned long cycle = pause + INCHING_PULSE_MS;
            const unsigned long phase = (elapsedMs - kick) % cycle;
            if (phase < pause) {
                return brake(); // worm gear + brake: defined, repeatable step size
            }
            return {INCHING_PULSE_DUTY, 0};
        }

        default:
            return classicOutput(elapsedMs, duty);
    }
}

inline unsigned long timeoutMs(int mode) {
    return sanitizeMode(mode) == DRIVE_MODE_CLASSIC ? CLASSIC_TIMEOUT_MS : SLOW_TIMEOUT_MS;
}

// Auto-calibration searches the lowest duty that still moves; only meaningful where the slider is a duty.
inline bool calibrationSupported(int mode) {
    return sanitizeMode(mode) != DRIVE_MODE_INCHING;
}

} // namespace MotorProfile

#endif
