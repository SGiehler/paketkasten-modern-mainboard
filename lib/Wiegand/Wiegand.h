#ifndef _WIEGAND_H
#define _WIEGAND_H

#include <Arduino.h>

struct WiegandFrameStats {
    uint64_t rawCode;
    uint8_t bitCount;
    uint16_t minBitGapUs;
    uint16_t maxBitGapUs;
    uint16_t durationMs;
    uint16_t rejectedPulses;
};

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
    WiegandFrameStats getFrameStats();
    uint32_t getSingleBitGlitchCount();

private:
    static void IRAM_ATTR data0ISR();
    static void IRAM_ATTR data1ISR();
    void IRAM_ATTR recordBitISR(uint8_t bit);
    void processCode();

    static Wiegand* instance;

    uint8_t _pinD0;
    uint8_t _pinD1;
    bool _initialized;

    volatile uint8_t _bitCount;
    volatile uint64_t _code;
    volatile bool _isReading;
    volatile unsigned long _firstBitMicros;
    volatile unsigned long _lastBitMicros;
    volatile uint16_t _minGapUs;
    volatile uint16_t _maxGapUs;
    volatile uint16_t _rejectedPulses;
    volatile uint32_t _singleBitGlitches;

    WiegandFrameStats _lastFrame;
    bool _available;

    portMUX_TYPE _wiegandMux;
};

#endif
