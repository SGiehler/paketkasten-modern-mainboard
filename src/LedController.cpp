#include "LedController.h"
#include "state.h"

LedController ledController(GREEN_LED_PIN, RED_LED_PIN);

LedController::LedController(int greenPin, int redPin) :
    _greenPin(greenPin),
    _redPin(redPin)
{}

void LedController::begin() {
    pinMode(_greenPin, OUTPUT);
    pinMode(_redPin, OUTPUT);
    digitalWrite(_greenPin, LOW);
    digitalWrite(_redPin, HIGH);
}

void LedController::update() {
    switch (currentState) {
        case LOCKED:
            digitalWrite(_greenPin, LOW);
            digitalWrite(_redPin, LOW);
            break;
        case PARCEL_OPEN:
        case MAIL_OPEN:
            digitalWrite(_greenPin, HIGH);
            digitalWrite(_redPin, LOW);
            break;
        case PRE_OPENING_TO_PARCEL:
        case OPENING_TO_PARCEL:
        case PRE_OPENING_TO_MAIL:
        case OPENING_TO_MAIL:
        case LOCKING:
            digitalWrite(_greenPin, LOW);
            digitalWrite(_redPin, HIGH);
            break;
        case MOTOR_ERROR:
            digitalWrite(_greenPin, HIGH);
            digitalWrite(_redPin, HIGH);
            break;
    }
}
