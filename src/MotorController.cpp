#include "MotorController.h"
#include "ConfigManager.h"
#include "state.h"

MotorController motorController(MOTOR_PIN_1, MOTOR_PIN_2);

static bool isMovingState(MailboxState state) {
    return state == OPENING_TO_PARCEL || state == OPENING_TO_MAIL || state == LOCKING;
}

MotorController::MotorController(int pin1, int pin2) :
    _pin1(pin1),
    _pin2(pin2),
    _lastMotorState((MailboxState)-1),
    _escalated(false)
{}

void MotorController::begin() {
    pinMode(_pin1, OUTPUT);
    pinMode(_pin2, OUTPUT);
    analogWriteFrequency(PWM_FREQ);
    analogWrite(_pin1, 0);
    analogWrite(_pin2, 0);
}

void MotorController::brakeNow() {
    noInterrupts();
    analogWrite(_pin1, MotorProfile::PWM_MAX);
    analogWrite(_pin2, MotorProfile::PWM_MAX);
    interrupts();
}

void MotorController::apply(const MotorOutput& out, bool opening) {
    // Opening drives IN1, locking drives IN2.
    const int in1 = opening ? out.drive : out.other;
    const int in2 = opening ? out.other : out.drive;
    noInterrupts();
    analogWrite(_pin1, in1);
    analogWrite(_pin2, in2);
    interrupts();
    // A limit switch ISR may have ended the movement between reading the state and writing the
    // drive values; brake right away instead of driving on until the next tick.
    if (!isMovingState(currentState)) {
        brakeNow();
    }
}

void MotorController::recordStroke(MailboxState from, MailboxState to) {
    unsigned long endTime;
    if (to == PARCEL_OPEN || to == MAIL_OPEN) {
        endTime = openStateEnterTime;
    } else if (to == LOCKED) {
        endTime = lockedStateEnterTime;
    } else {
        endTime = millis();
    }
    lastStrokeMs = endTime - motorStartTime;
    lastStrokeFrom = from;
    lastStrokeTo = to;
    lastStrokeEscalated = _escalated;

    const Config& config = configManager.getConfig();
    Serial.printf("[Motor] %s -> %s in %lu ms (mode=%d, open=%d, close=%d, kick=%d ms%s)\n",
                  mailboxStateName(from), mailboxStateName(to), (unsigned long)lastStrokeMs,
                  config.motorMode, config.dutyCycleOpen, config.dutyCycleClose, config.motorKickMs,
                  _escalated ? ", stall escalation used" : "");
}

void MotorController::update() {
    MailboxState state = currentState;

    if (state != _lastMotorState) {
        if (isMovingState(_lastMotorState)) {
            recordStroke(_lastMotorState, state);
        }
        if (isMovingState(state)) {
            motorStartTime = millis();
            _escalated = false;
        }
        _lastMotorState = state;
    }

    const Config& config = configManager.getConfig();
    const int mode = MotorProfile::sanitizeMode(config.motorMode);
    const unsigned long elapsedTime = millis() - motorStartTime;

    if (isMovingState(state) && elapsedTime > MotorProfile::timeoutMs(mode)) {
        currentState = MOTOR_ERROR;
        state = MOTOR_ERROR; // brake below; the stroke is recorded on the next tick
    }

    switch (state) {
        case OPENING_TO_PARCEL:
        case OPENING_TO_MAIL:
        case LOCKING: {
            const bool opening = (state != LOCKING);
            int baseDuty = opening
                ? MotorProfile::clampInt(config.dutyCycleOpen, MotorProfile::OPEN_DUTY_MIN, MotorProfile::OPEN_DUTY_MAX)
                : MotorProfile::clampInt(config.dutyCycleClose, MotorProfile::CLOSE_DUTY_MIN, MotorProfile::CLOSE_DUTY_MAX);

            // Calibration test steps probe a fixed candidate duty: no stall escalation there,
            // otherwise every candidate would "succeed".
            const bool calibrationProbe = calibrationActive &&
                ((opening && calibrationStep == 3) || (!opening && calibrationStep == 6));
            int duty;
            if (calibrationProbe) {
                duty = MotorProfile::clampInt(calibrationCandidateDuty, MotorProfile::DUTY_MIN, MotorProfile::DUTY_MAX);
            } else {
                duty = MotorProfile::escalatedDuty(mode, baseDuty, elapsedTime);
                if (duty > MotorProfile::clampInt(baseDuty, MotorProfile::DUTY_MIN, MotorProfile::DUTY_MAX)) {
                    _escalated = true;
                }
            }

            apply(MotorProfile::driveOutput(mode, elapsedTime, duty, config.motorKickMs), opening);
            break;
        }
        case LOCKED:
        case PARCEL_OPEN:
        case MAIL_OPEN:
        case MOTOR_ERROR:
            brakeNow();
            break;
        default:
            break;
    }
}
