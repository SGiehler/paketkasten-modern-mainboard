#include "SwitchManager.h"
#include "TelemetryLog.h"
#include "state.h"

SwitchManager switchManager(CLOSED_SWITCH_PIN, PARCEL_SWITCH_PIN, MAIL_SWITCH_PIN);

static volatile bool _gInvertState = false;

extern "C" void ets_delay_us(uint32_t us);

// Helper to filter out short high-frequency noise spikes in ISR and fast polling failsafe
bool IRAM_ATTR isPinPressedISR(int pin, int pressedState) {
    if (digitalRead(pin) != pressedState) return false;
    ets_delay_us(10);
    if (digitalRead(pin) != pressedState) return false;
    ets_delay_us(10);
    if (digitalRead(pin) != pressedState) return false;
    return true;
}

// Wake the app task so MotorController::update() brakes within microseconds instead of at the
// next 1 ms tick. The ISR cannot brake itself: LEDC owns the motor pins via the GPIO matrix,
// so digitalWrite() from here has no effect. vTaskNotifyGiveFromISR is IRAM-resident
// (CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH is not set in the Arduino core).
static void IRAM_ATTR wakeAppTaskFromISR() {
    if (appTaskHandle != nullptr) {
        BaseType_t higherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(appTaskHandle, &higherPriorityTaskWoken);
        if (higherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

void IRAM_ATTR closedSwitchISR() {
    int pressedState = _gInvertState ? HIGH : LOW;
    if (isPinPressedISR(CLOSED_SWITCH_PIN, pressedState)) {
        bool woke = false;
        portENTER_CRITICAL_ISR(&stateMux);
        if (currentState == LOCKING) {
            currentState = LOCKED;
            lockedStateEnterTime = millis();
            closedAlreadyReachedInCycle = true;
            woke = true;
        }
        portEXIT_CRITICAL_ISR(&stateMux);
        if (woke) {
            wakeAppTaskFromISR();
        }
    }
}

void IRAM_ATTR parcelSwitchISR() {
    int pressedState = _gInvertState ? HIGH : LOW;
    if (isPinPressedISR(PARCEL_SWITCH_PIN, pressedState)) {
        bool woke = false;
        portENTER_CRITICAL_ISR(&stateMux);
        if (currentState == OPENING_TO_PARCEL) {
            currentState = PARCEL_OPEN;
            openStateEnterTime = millis();
            woke = true;
        }
        portEXIT_CRITICAL_ISR(&stateMux);
        if (woke) {
            wakeAppTaskFromISR();
        }
    }
}

void IRAM_ATTR mailSwitchISR() {
    int pressedState = _gInvertState ? HIGH : LOW;
    if (isPinPressedISR(MAIL_SWITCH_PIN, pressedState)) {
        bool woke = false;
        portENTER_CRITICAL_ISR(&stateMux);
        if (currentState == OPENING_TO_MAIL || currentState == OPENING_TO_PARCEL) {
            currentState = MAIL_OPEN;
            openStateEnterTime = millis();
            woke = true;
        }
        portEXIT_CRITICAL_ISR(&stateMux);
        if (woke) {
            wakeAppTaskFromISR();
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
    const int pressedState = _invertState ? HIGH : LOW;
    
    _closedSwitch.attach(_closedPin, _invertState ? INPUT_PULLDOWN : INPUT_PULLUP);
    _parcelSwitch.attach(_parcelPin, _invertState ? INPUT_PULLDOWN : INPUT_PULLUP);
    _mailSwitch.attach(_mailPin, _invertState ? INPUT_PULLDOWN : INPUT_PULLUP);

    _closedSwitch.interval(debounceDelayMs);
    _parcelSwitch.interval(debounceDelayMs);
    _mailSwitch.interval(debounceDelayMs);

    _closedSwitch.setPressedState(pressedState);
    _parcelSwitch.setPressedState(pressedState);
    _mailSwitch.setPressedState(pressedState);

    // Seed Bounce2 and initial state from physical Hall sensors at boot so a reboot
    // while physically open is detected instead of blindly assuming LOCKED.
    _closedSwitch.update();
    _parcelSwitch.update();
    _mailSwitch.update();

    bool bootClosed = isPinPressedISR(_closedPin, pressedState);
    bool bootParcel = isPinPressedISR(_parcelPin, pressedState);
    bool bootMail = isPinPressedISR(_mailPin, pressedState);

    portENTER_CRITICAL(&stateMux);
    if (bootMail) {
        currentState = MAIL_OPEN;
        openStateEnterTime = millis();
        closedAlreadyReachedInCycle = false;
    } else if (bootParcel) {
        currentState = PARCEL_OPEN;
        openStateEnterTime = millis();
        closedAlreadyReachedInCycle = false;
    } else {
        currentState = LOCKED;
        lockedStateEnterTime = millis();
        closedAlreadyReachedInCycle = bootClosed;
    }
    _lastState = currentState;
    portEXIT_CRITICAL(&stateMux);

    _gInvertState = _invertState;
    attachInterrupt(digitalPinToInterrupt(_closedPin), closedSwitchISR, _invertState ? RISING : FALLING);
    attachInterrupt(digitalPinToInterrupt(_parcelPin), parcelSwitchISR, _invertState ? RISING : FALLING);
    attachInterrupt(digitalPinToInterrupt(_mailPin), mailSwitchISR, _invertState ? RISING : FALLING);

    telemetryLog.logf("SWITCH", "Boot sensors: closed=%d parcel=%d mail=%d -> state=%s",
                      bootClosed ? 1 : 0, bootParcel ? 1 : 0, bootMail ? 1 : 0,
                      mailboxStateName(currentState));
}

void SwitchManager::update() {
    _closedSwitch.update();
    _parcelSwitch.update();
    _mailSwitch.update();

    const int pressedState = _invertState ? HIGH : LOW;
    MailboxState state = currentState;

    // Fast 20us-filtered level-triggered failsafe during active movement:
    // If an EMI glitch caused the single FALLING edge ISR to reject the edge, catch the
    // steady active sensor level within <=1ms instead of waiting for Bounce2 (2-3ms).
    switch (state) {
        case OPENING_TO_PARCEL:
            if (_parcelSwitch.isPressed() || isPinPressedISR(_parcelPin, pressedState)) {
                portENTER_CRITICAL(&stateMux);
                if (currentState == OPENING_TO_PARCEL) {
                    currentState = PARCEL_OPEN;
                    openStateEnterTime = millis();
                }
                portEXIT_CRITICAL(&stateMux);
            } else if (_mailSwitch.isPressed() || isPinPressedISR(_mailPin, pressedState)) {
                bool overshot = false;
                portENTER_CRITICAL(&stateMux);
                if (currentState == OPENING_TO_PARCEL) {
                    currentState = MAIL_OPEN;
                    openStateEnterTime = millis();
                    overshot = true;
                }
                portEXIT_CRITICAL(&stateMux);
                if (overshot) {
                    telemetryLog.logf("SWITCH", "Failsafe: overshoot during OPENING_TO_PARCEL, stopped at mail switch");
                }
            }
            break;
        case OPENING_TO_MAIL:
            if (_mailSwitch.isPressed() || isPinPressedISR(_mailPin, pressedState)) {
                portENTER_CRITICAL(&stateMux);
                if (currentState == OPENING_TO_MAIL) {
                    currentState = MAIL_OPEN;
                    openStateEnterTime = millis();
                }
                portEXIT_CRITICAL(&stateMux);
            }
            break;
        case LOCKING:
            if (_closedSwitch.isPressed() || isPinPressedISR(_closedPin, pressedState)) {
                portENTER_CRITICAL(&stateMux);
                if (currentState == LOCKING) {
                    currentState = LOCKED;
                    lockedStateEnterTime = millis();
                    closedAlreadyReachedInCycle = true;
                }
                portEXIT_CRITICAL(&stateMux);
            }
            break;
        default:
            break;
    }

    MailboxState postState = currentState;
    if (postState != _lastState) {
        publishState();
        _lastState = postState;
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
