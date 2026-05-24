#ifndef _WIEGAND_H
#define _WIEGAND_H

#include <Arduino.h>

class Wiegand {
public:
    Wiegand();
    void begin(uint8_t pinD0, uint8_t pinD1);
    void attach();
    void detach();
    void loop();
    bool isAvailable();
    uint64_t getCode();
    uint8_t getBitCount();

private:
    static void IRAM_ATTR data0ISR();
    static void IRAM_ATTR data1ISR();
    void processCode();

    static Wiegand* instance;

    uint8_t _pinD0;
    uint8_t _pinD1;

    volatile uint8_t _bitCount;
    volatile uint64_t _code;
    
    volatile bool _isReading;
    volatile unsigned long _lastBitMicros;

    uint64_t _lastCode;
    uint8_t _lastBitCount;
    bool _available;

    portMUX_TYPE _wiegandMux;
};

#endif
