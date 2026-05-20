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
    uint32_t getCode();
    uint8_t getBitCount();

    // Task handle for ISR-to-task notification (set by the FreeRTOS task)
    volatile TaskHandle_t _taskHandle;

private:
    static void IRAM_ATTR data0ISR();
    static void IRAM_ATTR data1ISR();
    void processCode();

    static Wiegand* instance;

    uint8_t _pinD0;
    uint8_t _pinD1;

    volatile uint8_t _bitCount;
    volatile uint32_t _code;
    
    volatile bool _isReading;
    volatile unsigned long _lastWakeMicros;

    uint32_t _lastCode;
    uint8_t _lastBitCount;
    bool _available;
};

#endif
