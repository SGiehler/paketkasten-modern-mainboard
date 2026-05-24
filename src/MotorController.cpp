#include "MotorController.h"
#include "ConfigManager.h"
#include "state.h"

MotorController motorController(MOTOR_PIN_1, MOTOR_PIN_2);

MotorController::MotorController(int pin1, int pin2) :
    _pin1(pin1),
    _pin2(pin2),
    _lastMotorState((MailboxState)-1)
{}

void MotorController::begin() {
    pinMode(_pin1, OUTPUT);
    pinMode(_pin2, OUTPUT);
    analogWriteFrequency(_pin1, PWM_FREQ);
    analogWriteFrequency(_pin2, PWM_FREQ);
    analogWrite(_pin1, 0);
    analogWrite(_pin2, 0);
}

void MotorController::update() {
    if ((currentState == OPENING_TO_PARCEL || currentState == OPENING_TO_MAIL || currentState == LOCKING) && _lastMotorState != currentState) {
        motorStartTime = millis();
    }
    _lastMotorState = currentState;

    if ((currentState == OPENING_TO_PARCEL || currentState == OPENING_TO_MAIL || currentState == LOCKING) && (millis() - motorStartTime > 2000)) {
        currentState = MOTOR_ERROR;
    }

    unsigned long elapsedTime = millis() - motorStartTime;
    int dutyCycle;
    Config& config = configManager.getConfig();

    switch (currentState) {
        case OPENING_TO_PARCEL:
        case OPENING_TO_MAIL:
            dutyCycle = config.dutyCycleOpen;
            if (elapsedTime < FULL_POWER_MS) {
                dutyCycle = FULL_POWER_DUTY_CYCLE;
            } else if (elapsedTime < (RAMP_DOWN_MS + FULL_POWER_MS)) {
                dutyCycle = 255 - (255 - config.dutyCycleOpen) * (elapsedTime - 100) / RAMP_DOWN_MS;
            }
            noInterrupts();
            analogWrite(_pin1, dutyCycle);
            analogWrite(_pin2, 0);
            interrupts();
            break;
        case LOCKING:
            dutyCycle = config.dutyCycleClose;
            if (elapsedTime < FULL_POWER_MS) {
                dutyCycle = FULL_POWER_DUTY_CYCLE;
            } else if (elapsedTime < (RAMP_DOWN_MS + FULL_POWER_MS)) { 
                dutyCycle = 255 - (255 - config.dutyCycleClose) * (elapsedTime - 50) / RAMP_DOWN_MS;
            }
            noInterrupts();
            analogWrite(_pin1, 0);
            analogWrite(_pin2, dutyCycle);
            interrupts();
            break;
        case LOCKED:
        case PARCEL_OPEN:
        case MAIL_OPEN:
        case MOTOR_ERROR:
            noInterrupts();
            analogWrite(_pin1, 255);
            analogWrite(_pin2, 255);
            interrupts();
            break;
    }
}
