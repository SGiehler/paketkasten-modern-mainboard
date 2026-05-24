#include "SwitchManager.h"
#include "state.h"

SwitchManager switchManager(CLOSED_SWITCH_PIN, PARCEL_SWITCH_PIN, MAIL_SWITCH_PIN);

static volatile bool _gInvertState = false;

extern "C" void ets_delay_us(uint32_t us);

// Helper to filter out short high-frequency noise spikes in ISR
bool IRAM_ATTR isPinPressedISR(int pin, int pressedState) {
    if (digitalRead(pin) != pressedState) return false;
    ets_delay_us(10);
    if (digitalRead(pin) != pressedState) return false;
    ets_delay_us(10);
    if (digitalRead(pin) != pressedState) return false;
    return true;
}

void IRAM_ATTR closedSwitchISR() {
    int pressedState = _gInvertState ? HIGH : LOW;
    if (isPinPressedISR(CLOSED_SWITCH_PIN, pressedState)) {
        if (currentState == LOCKING) {
            currentState = LOCKED;
            lockedStateEnterTime = millis();
            wiegandAttached = false;
            digitalWrite(MOTOR_PIN_1, HIGH);
            digitalWrite(MOTOR_PIN_2, HIGH);
        }
    }
}

void IRAM_ATTR parcelSwitchISR() {
    int pressedState = _gInvertState ? HIGH : LOW;
    if (isPinPressedISR(PARCEL_SWITCH_PIN, pressedState)) {
        if (currentState == OPENING_TO_PARCEL) {
            currentState = PARCEL_OPEN;
            openStateEnterTime = millis();
            digitalWrite(MOTOR_PIN_1, HIGH);
            digitalWrite(MOTOR_PIN_2, HIGH);
        }
    }
}

void IRAM_ATTR mailSwitchISR() {
    int pressedState = _gInvertState ? HIGH : LOW;
    if (isPinPressedISR(MAIL_SWITCH_PIN, pressedState)) {
        if (currentState == OPENING_TO_MAIL) {
            currentState = MAIL_OPEN;
            openStateEnterTime = millis();
            digitalWrite(MOTOR_PIN_1, HIGH);
            digitalWrite(MOTOR_PIN_2, HIGH);
        } else if (currentState == OPENING_TO_PARCEL) {
            currentState = MAIL_OPEN;
            openStateEnterTime = millis();
            digitalWrite(MOTOR_PIN_1, HIGH);
            digitalWrite(MOTOR_PIN_2, HIGH);
        }
    }
}

SwitchManager::SwitchManager(int closedPin, int parcelPin, int mailPin) :
    _closedPin(closedPin),
    _parcelPin(parcelPin),
    _mailPin(mailPin),
    _invertState(false),
    _lastState(LOCKED)
{}

void SwitchManager::begin(int debounceDelayMs, bool invertState) {
    _invertState = invertState;
    
    _closedSwitch.attach(_closedPin, _invertState ? INPUT_PULLDOWN : INPUT_PULLUP);
    _parcelSwitch.attach(_parcelPin, _invertState ? INPUT_PULLDOWN : INPUT_PULLUP);
    _mailSwitch.attach(_mailPin, _invertState ? INPUT_PULLDOWN : INPUT_PULLUP);

    _closedSwitch.interval(debounceDelayMs);
    _parcelSwitch.interval(debounceDelayMs);
    _mailSwitch.interval(debounceDelayMs);

    _closedSwitch.setPressedState(_invertState ? HIGH : LOW);
    _parcelSwitch.setPressedState(_invertState ? HIGH : LOW);
    _mailSwitch.setPressedState(_invertState ? HIGH : LOW);
    
    _lastState = currentState;

    _gInvertState = _invertState;
    attachInterrupt(digitalPinToInterrupt(_closedPin), closedSwitchISR, _invertState ? RISING : FALLING);
    attachInterrupt(digitalPinToInterrupt(_parcelPin), parcelSwitchISR, _invertState ? RISING : FALLING);
    attachInterrupt(digitalPinToInterrupt(_mailPin), mailSwitchISR, _invertState ? RISING : FALLING);
}

void SwitchManager::update() {
    _closedSwitch.update();
    _parcelSwitch.update();
    _mailSwitch.update();

    switch (currentState) {
        case OPENING_TO_PARCEL:
            if (_parcelSwitch.isPressed()) {
                currentState = PARCEL_OPEN;
                openStateEnterTime = millis();
            } else if (_mailSwitch.isPressed()) {
                // Failsafe: if we overshoot the parcel switch and hit the mail switch, stop there!
                currentState = MAIL_OPEN;
                openStateEnterTime = millis();
                Serial.println("Failsafe: overshoot detected during OPENING_TO_PARCEL, stopped at mail switch");
            }
            break;
        case OPENING_TO_MAIL:
            if (_mailSwitch.isPressed()) {
                currentState = MAIL_OPEN;
                openStateEnterTime = millis();
            }
            break;
        case LOCKING:
            if (_closedSwitch.isPressed()) {
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
    return _closedSwitch.isPressed();
}

bool SwitchManager::isParcelPressed() {
    return _parcelSwitch.isPressed();
}

bool SwitchManager::isMailPressed() {
    return _mailSwitch.isPressed();
}
