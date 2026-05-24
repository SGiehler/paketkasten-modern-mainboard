#ifndef WIEGAND_MANAGER_H
#define WIEGAND_MANAGER_H

#include <Arduino.h>
#include <Wiegand.h>

class WiegandManager {
public:
    WiegandManager(int d0Pin, int d1Pin);
    void begin(void (*onCodeCallback)(char* code, uint8_t bits));
    void update();
    void attach();
    void detach();
    bool isAttached() const { return _attached; }

private:
    int _d0Pin;
    int _d1Pin;
    bool _attached;
    void (*_onCodeCallback)(char* code, uint8_t bits);
    Wiegand _wiegand;

    char _keypadPinStr[20];
    uint8_t _keypadPinLen;
    unsigned long _lastKeypadPressTime;
};

extern WiegandManager wiegandManager;

#endif
