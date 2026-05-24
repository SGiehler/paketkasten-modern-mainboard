#ifndef SWITCH_MANAGER_H
#define SWITCH_MANAGER_H

#include <Bounce2.h>
#include "state.h"

class SwitchManager {
public:
    SwitchManager(int closedPin, int parcelPin, int mailPin);
    void begin(int debounceDelayMs = 1, bool invertState = false);
    void update();
    
    // Status helpers for diagnostics page and status checks
    bool isClosedPressed();
    bool isParcelPressed();
    bool isMailPressed();

private:
    Bounce2::Button _closedSwitch;
    Bounce2::Button _parcelSwitch;
    Bounce2::Button _mailSwitch;
    int _closedPin;
    int _parcelPin;
    int _mailPin;
    bool _invertState;
    MailboxState _lastState;
};

extern SwitchManager switchManager;

#endif
