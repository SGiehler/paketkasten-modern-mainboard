#include "MotorController.h"
#include "ConfigManager.h"
#include "TelemetryLog.h"
#include "state.h"

MotorController motorController(MOTOR_PIN_1, MOTOR_PIN_2);

static bool isMovingState(MailboxState state) {
    return state == OPENING_TO_PARCEL || state == OPENING_TO_MAIL || state == LOCKING;
}

MotorController::MotorController(int pin1, int pin2) :
    _pin1(pin1),
    _pin2(pin2),
    _lastIn1(-1),
    _lastIn2(-1),
    _lastMotorState((MailboxState)-1),
    _brakeStartTime(0),
    _brakeActive(false),
    _escalated(false)
{}

void MotorController::begin() {
    // Configure dedicated High-Speed LEDC channels ONCE (Timer 1, isolated from tone() on Timer 0).
    ledcSetup(MOTOR_LEDC_CH1, PWM_FREQ, MOTOR_LEDC_RES);
    ledcSetup(MOTOR_LEDC_CH2, PWM_FREQ, MOTOR_LEDC_RES);
    ledcAttachPin(_pin1, MOTOR_LEDC_CH1);
    ledcAttachPin(_pin2, MOTOR_LEDC_CH2);

    _lastIn1 = -1;
    _lastIn2 = -1;
    _brakeStartTime = 0;
    _brakeActive = false;
    writePins(0, 0);
}

void MotorController::writePins(int in1, int in2) {
    if (in1 == _lastIn1 && in2 == _lastIn2) {
        return;
    }
    ledcWrite(MOTOR_LEDC_CH1, in1);
    ledcWrite(MOTOR_LEDC_CH2, in2);
    _lastIn1 = in1;
    _lastIn2 = in2;
}

void MotorController::brakeNow() {
    MotorOutput out = MotorProfile::brake();
    writePins(out.drive, out.other);
}

void MotorController::apply(const MotorOutput& out, bool opening) {
    const int in1 = opening ? out.drive : out.other;
    const int in2 = opening ? out.other : out.drive;
    writePins(in1, in2);

    // A limit switch ISR may have ended the movement between reading the state and writing
    // drive values; brake right away instead of driving on until the next tick.
    if (!isMovingState(currentState)) {
        brakeNow();
    }
}

void MotorController::recordStroke(MailboxState from, MailboxState to) {
    unsigned long now = millis();
    unsigned long endTime = now;
    if (to == PARCEL_OPEN || to == MAIL_OPEN) {
        endTime = openStateEnterTime;
    } else if (to == LOCKED) {
        endTime = lockedStateEnterTime;
    }
    unsigned long duration = (endTime >= motorStartTime) ? (endTime - motorStartTime) : (now - motorStartTime);
    lastStrokeMs = duration;
    lastStrokeFrom = from;
    lastStrokeTo = to;
    lastStrokeEscalated = _escalated;

    const MotorSettings settings = configManager.getMotorSettings();
    telemetryLog.logf("MOTOR", "%s -> %s in %lu ms (open=%d close=%d kick=%dms%s)",
                      mailboxStateName(from), mailboxStateName(to), (unsigned long)lastStrokeMs,
                      settings.dutyCycleOpen, settings.dutyCycleClose, settings.motorKickMs,
                      _escalated ? " ESCALATED" : "");
}

void MotorController::update() {
    MailboxState state = currentState;

    if (state != _lastMotorState) {
        const MailboxState prevState = _lastMotorState;
        _lastMotorState = state;

        if (isMovingState(prevState) && !isMovingState(state)) {
            // Brake IMMEDIATELY before logging so rotor inertia is killed at once.
            brakeNow();
            _brakeStartTime = millis();
            _brakeActive = true;
            recordStroke(prevState, state);
        }

        if (isMovingState(state)) {
            motorStartTime = millis();
            _escalated = false;
            _brakeActive = false;
        }
    }

    const MotorSettings settings = configManager.getMotorSettings();
    const unsigned long elapsedTime = millis() - motorStartTime;

    if (isMovingState(state) && elapsedTime > MotorProfile::TIMEOUT_MS) {
        bool transitioned = false;
        portENTER_CRITICAL(&stateMux);
        if (currentState == state) {
            currentState = MOTOR_ERROR;
            transitioned = true;
        }
        state = currentState;
        portEXIT_CRITICAL(&stateMux);

        if (transitioned || !isMovingState(state)) {
            brakeNow();
            _brakeStartTime = millis();
            _brakeActive = true;
            recordStroke(_lastMotorState, state);
            _lastMotorState = state;
            return;
        }
    }

    switch (state) {
        case OPENING_TO_PARCEL:
        case OPENING_TO_MAIL:
        case LOCKING: {
            const bool opening = (state != LOCKING);
            int baseDuty = opening
                ? MotorProfile::clampInt(settings.dutyCycleOpen, MotorProfile::OPEN_DUTY_MIN, MotorProfile::OPEN_DUTY_MAX)
                : MotorProfile::clampInt(settings.dutyCycleClose, MotorProfile::CLOSE_DUTY_MIN, MotorProfile::CLOSE_DUTY_MAX);

            int duty = MotorProfile::escalatedDuty(baseDuty, elapsedTime, opening);
            if (duty > MotorProfile::clampInt(baseDuty, MotorProfile::DUTY_MIN, MotorProfile::DUTY_MAX)) {
                _escalated = true;
            }

            apply(MotorProfile::driveOutput(elapsedTime, duty, settings.motorKickMs, opening), opening);
            break;
        }
        case LOCKED:
        case PARCEL_OPEN:
        case MAIL_OPEN:
        case MOTOR_ERROR:
        case PRE_OPENING_TO_PARCEL:
        case PRE_OPENING_TO_MAIL: {
            if (_brakeActive) {
                MotorOutput out = MotorProfile::idleOutput(millis() - _brakeStartTime);
                writePins(out.drive, out.other);
                if (out.drive == 0 && out.other == 0) {
                    _brakeActive = false;
                }
            } else {
                writePins(0, 0);
            }
            break;
        }
        default:
            writePins(0, 0);
            break;
    }
}
