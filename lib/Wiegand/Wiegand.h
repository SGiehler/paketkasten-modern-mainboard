#ifndef _WIEGAND_H
#define _WIEGAND_H

#include <Arduino.h>

class Wiegand {
public:
    Wiegand();
    void begin(uint8_t pinD0, uint8_t pinD1);
    void loop();
    bool isAvailable();
    uint32_t getCode();
    uint8_t getBitCount();

private:
    static void IRAM_ATTR data0ISR();
    static void IRAM_ATTR data1ISR();
    void handleData(uint8_t data);
    void processCode();

    static Wiegand* instance;

    uint8_t _pinD0;
    uint8_t _pinD1;

    volatile uint8_t _bitCount;
    volatile uint32_t _code;
    volatile unsigned long _lastBitTime;

    uint32_t _lastCode;
    uint8_t _lastBitCount;
    bool _available;
};

#endif
