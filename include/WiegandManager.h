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
};

extern WiegandManager wiegandManager;

#endif
