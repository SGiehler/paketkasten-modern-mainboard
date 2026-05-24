#include "SwitchManager.h"
#include "state.h"

SwitchManager switchManager(CLOSED_SWITCH_PIN, PARCEL_SWITCH_PIN, MAIL_SWITCH_PIN);

SwitchManager::SwitchManager(int closedPin, int parcelPin, int mailPin) :
    _closedPin(closedPin),
    _parcelPin(parcelPin),
    _mailPin(mailPin),
    _invertState(false),
    _lastState(LOCKED)
{}

void SwitchManager::begin(int debounceDelayMs, bool invertState) {
    _invertState = invertState;
    
    _closedSwitch.attach(_closedPin, INPUT);
    _parcelSwitch.attach(_parcelPin, INPUT);
    _mailSwitch.attach(_mailPin, INPUT);

    _closedSwitch.interval(debounceDelayMs);
    _parcelSwitch.interval(debounceDelayMs);
    _mailSwitch.interval(debounceDelayMs);

    _closedSwitch.setPressedState(_invertState ? HIGH : LOW);
    _parcelSwitch.setPressedState(_invertState ? HIGH : LOW);
    _mailSwitch.setPressedState(_invertState ? HIGH : LOW);
    
    _lastState = currentState;
}

void SwitchManager::update() {
    _closedSwitch.update();
    _parcelSwitch.update();
    _mailSwitch.update();

    switch (currentState) {
        case OPENING_TO_PARCEL:
            if (_parcelSwitch.pressed()) {
                currentState = PARCEL_OPEN;
                openStateEnterTime = millis();
            }
            break;
        case OPENING_TO_MAIL:
            if (_mailSwitch.pressed()) {
                currentState = MAIL_OPEN;
                openStateEnterTime = millis();
            }
            break;
        case LOCKING:
            if (_closedSwitch.pressed()) {
                currentState = LOCKED;
                lockedStateEnterTime = millis();
                wiegandAttached = false;
            }
            break;
        default:
            break;
    }

    if (currentState != _lastState) {
        publishState();
        _lastState = currentState;
    }
}

bool SwitchManager::isClosedPressed() {
    return digitalRead(_closedPin) == (_invertState ? HIGH : LOW);
}

bool SwitchManager::isParcelPressed() {
    return digitalRead(_parcelPin) == (_invertState ? HIGH : LOW);
}

bool SwitchManager::isMailPressed() {
    return digitalRead(_mailPin) == (_invertState ? HIGH : LOW);
}
