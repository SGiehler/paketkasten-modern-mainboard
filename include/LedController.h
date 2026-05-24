#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>

class LedController {
public:
    LedController(int greenPin, int redPin);
    void begin();
    void update();

private:
    int _greenPin;
    int _redPin;
};

extern LedController ledController;

#endif
