#include "Wiegand.h"
#include <cstring>
#ifdef ESP32
#include <esp_timer.h>
static inline unsigned long IRAM_ATTR wiegandMicros() {
    return (unsigned long)esp_timer_get_time();
}
#else
static inline unsigned long wiegandMicros() {
    return micros();
}
#endif

Wiegand* Wiegand::instance = nullptr;

// Standard Wiegand pulses are 20-100us wide, spaced 1000-2000us apart.
// A 200us inter-bit lockout ignores rising-edge ringing on long cables without
// ever dropping valid bits and without calling digitalRead() inside the ISR
// (a 20-50us pulse may already have returned HIGH by the time the ISR enters).
static constexpr unsigned long WIEGAND_MIN_BIT_INTERVAL_US = 200;
static constexpr unsigned long WIEGAND_GLITCH_RESET_GAP_US = 15000; // Restart frame if <4 stray edges preceded a swipe by >15ms
static constexpr unsigned long WIEGAND_FRAME_SILENCE_US = 25000;    // 25ms (>12x standard 2ms bit spacing)

Wiegand::Wiegand() {
    instance = this;
    _pinD0 = 0xFF;
    _pinD1 = 0xFF;
    _initialized = false;
    _bitCount = 0;
    _code = 0;
    _isReading = false;
    _firstBitMicros = 0;
    _lastBitMicros = 0;
    _minGapUs = 0xFFFF;
    _maxGapUs = 0;
    _rejectedPulses = 0;
    _singleBitGlitches = 0;
    memset(&_lastFrame, 0, sizeof(_lastFrame));
    _available = false;
    _wiegandMux = portMUX_INITIALIZER_UNLOCKED;
}

void Wiegand::begin(uint8_t pinD0, uint8_t pinD1) {
    _pinD0 = pinD0;
    _pinD1 = pinD1;
    _initialized = true;

    pinMode(_pinD0, INPUT_PULLUP);
    pinMode(_pinD1, INPUT_PULLUP);

    attach();
}

void Wiegand::attach() {
    if (!_initialized) {
        return;
    }

    portENTER_CRITICAL(&_wiegandMux);
    _bitCount = 0;
    _code = 0;
    _isReading = false;
    _firstBitMicros = 0;
    _lastBitMicros = 0;
    _minGapUs = 0xFFFF;
    _maxGapUs = 0;
    _rejectedPulses = 0;
    _available = false;
    portEXIT_CRITICAL(&_wiegandMux);

    attachInterrupt(digitalPinToInterrupt(_pinD0), data0ISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(_pinD1), data1ISR, FALLING);
}

void Wiegand::detach() {
    if (!_initialized) {
        return;
    }

    detachInterrupt(digitalPinToInterrupt(_pinD0));
    detachInterrupt(digitalPinToInterrupt(_pinD1));

    portENTER_CRITICAL(&_wiegandMux);
    _bitCount = 0;
    _code = 0;
    _isReading = false;
    _firstBitMicros = 0;
    _lastBitMicros = 0;
    _minGapUs = 0xFFFF;
    _maxGapUs = 0;
    _rejectedPulses = 0;
    _available = false;
    portEXIT_CRITICAL(&_wiegandMux);
}

void IRAM_ATTR Wiegand::recordBitISR(uint8_t bit) {
    portENTER_CRITICAL_ISR(&_wiegandMux);
    unsigned long now = wiegandMicros();
    if (!_isReading) {
        _code = bit ? 1ULL : 0ULL;
        _bitCount = 1;
        _firstBitMicros = now;
        _lastBitMicros = now;
        _minGapUs = 0xFFFF;
        _maxGapUs = 0;
        _rejectedPulses = 0;
        _isReading = true;
    } else {
        unsigned long gap = now - _lastBitMicros;
        if (_bitCount < 4 && gap > WIEGAND_GLITCH_RESET_GAP_US) {
            // Prior 1-3 stray edges were isolated noise; restart cleanly on this real frame's first bit.
            _singleBitGlitches = _singleBitGlitches + 1;
            _code = bit ? 1ULL : 0ULL;
            _bitCount = 1;
            _firstBitMicros = now;
            _lastBitMicros = now;
            _minGapUs = 0xFFFF;
            _maxGapUs = 0;
            _rejectedPulses = 0;
        } else if (gap > WIEGAND_MIN_BIT_INTERVAL_US) {
            if (_bitCount < 64) {
                _code = (_code << 1) | (bit ? 1ULL : 0ULL);
                _bitCount = _bitCount + 1;
            }
            uint16_t clampedGap = (gap > 65535UL) ? 65535U : (uint16_t)gap;
            if (clampedGap < _minGapUs) _minGapUs = clampedGap;
            if (clampedGap > _maxGapUs) _maxGapUs = clampedGap;
            _lastBitMicros = now;
        } else {
            if (_rejectedPulses < 65535U) {
                _rejectedPulses = _rejectedPulses + 1;
            }
        }
    }
    portEXIT_CRITICAL_ISR(&_wiegandMux);
}

void IRAM_ATTR Wiegand::data0ISR() {
    if (instance && instance->_initialized) {
        instance->recordBitISR(0);
    }
}

void IRAM_ATTR Wiegand::data1ISR() {
    if (instance && instance->_initialized) {
        instance->recordBitISR(1);
    }
}

void Wiegand::loop() {
    portENTER_CRITICAL(&_wiegandMux);
    if (_isReading) {
        unsigned long now = wiegandMicros();
        if (now - _lastBitMicros > WIEGAND_FRAME_SILENCE_US) {
            processCode();
            _isReading = false;
        }
    }
    portEXIT_CRITICAL(&_wiegandMux);
}

void Wiegand::processCode() {
    // Capture every frame >= 2 bits into _lastFrame so short/corrupted frames are visible in telemetry
    if (_bitCount >= 2) {
        _lastFrame.rawCode = _code;
        _lastFrame.bitCount = _bitCount;
        _lastFrame.minBitGapUs = (_minGapUs == 0xFFFF) ? 0 : _minGapUs;
        _lastFrame.maxBitGapUs = _maxGapUs;
        unsigned long durUs = _lastBitMicros - _firstBitMicros;
        _lastFrame.durationMs = (durUs / 1000UL > 65535UL) ? 65535U : (uint16_t)(durUs / 1000UL);
        _lastFrame.rejectedPulses = _rejectedPulses;
        _available = true;
    } else if (_bitCount == 1) {
        _singleBitGlitches = _singleBitGlitches + 1;
    }
    _code = 0;
    _bitCount = 0;
    _minGapUs = 0xFFFF;
    _maxGapUs = 0;
    _rejectedPulses = 0;
}

bool Wiegand::isAvailable() {
    portENTER_CRITICAL(&_wiegandMux);
    bool avail = _available;
    _available = false;
    portEXIT_CRITICAL(&_wiegandMux);
    return avail;
}

uint64_t Wiegand::getCode() {
    portENTER_CRITICAL(&_wiegandMux);
    uint64_t code = _lastFrame.rawCode;
    portEXIT_CRITICAL(&_wiegandMux);
    return code;
}

uint8_t Wiegand::getBitCount() {
    portENTER_CRITICAL(&_wiegandMux);
    uint8_t bits = _lastFrame.bitCount;
    portEXIT_CRITICAL(&_wiegandMux);
    return bits;
}

WiegandFrameStats Wiegand::getFrameStats() {
    portENTER_CRITICAL(&_wiegandMux);
    WiegandFrameStats stats = _lastFrame;
    portEXIT_CRITICAL(&_wiegandMux);
    return stats;
}

uint32_t Wiegand::getSingleBitGlitchCount() {
    portENTER_CRITICAL(&_wiegandMux);
    uint32_t count = _singleBitGlitches;
    portEXIT_CRITICAL(&_wiegandMux);
    return count;
}
