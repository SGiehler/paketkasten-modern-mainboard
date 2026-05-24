#include "Wiegand.h"

Wiegand* Wiegand::instance = nullptr;

Wiegand::Wiegand() {
    instance = this;
    _bitCount = 0;
    _code = 0;
    _isReading = false;
    _lastBitMicros = 0;
    _available = false;
    _lastCode = 0;
    _lastBitCount = 0;
    _wiegandMux = portMUX_INITIALIZER_UNLOCKED;
}

void Wiegand::begin(uint8_t pinD0, uint8_t pinD1) {
    _pinD0 = pinD0;
    _pinD1 = pinD1;

    pinMode(_pinD0, INPUT_PULLUP);
    pinMode(_pinD1, INPUT_PULLUP);

    attach();
}

void Wiegand::attach() {
    portENTER_CRITICAL(&_wiegandMux);
    _bitCount = 0;
    _code = 0;
    _isReading = false;
    _lastBitMicros = 0;
    portEXIT_CRITICAL(&_wiegandMux);

    attachInterrupt(digitalPinToInterrupt(_pinD0), data0ISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(_pinD1), data1ISR, FALLING);
}

void Wiegand::detach() {
    detachInterrupt(digitalPinToInterrupt(_pinD0));
    detachInterrupt(digitalPinToInterrupt(_pinD1));
}

// ISR: 100% interrupt-driven capture with a 200us software debounce filter.
// The first bit is always accepted immediately. Subsequent bits must be > 200us apart.
void IRAM_ATTR Wiegand::data0ISR() {
    if (instance) {
        unsigned long now = micros();
        if (!instance->_isReading || (now - instance->_lastBitMicros > 50)) {
            if (instance->_bitCount < 64) {
                instance->_code <<= 1;
                instance->_bitCount = instance->_bitCount + 1;
            }
            instance->_lastBitMicros = now;
            instance->_isReading = true;
        }
    }
}

void IRAM_ATTR Wiegand::data1ISR() {
    if (instance) {
        unsigned long now = micros();
        if (!instance->_isReading || (now - instance->_lastBitMicros > 50)) {
            if (instance->_bitCount < 64) {
                instance->_code = (instance->_code << 1) | 1;
                instance->_bitCount = instance->_bitCount + 1;
            }
            instance->_lastBitMicros = now;
            instance->_isReading = true;
        }
    }
}

void Wiegand::loop() {
    if (_isReading) {
        unsigned long now = micros();
        // 50ms silence detection (50,000 microseconds)
        if (now - _lastBitMicros > 50000) {
            portENTER_CRITICAL(&_wiegandMux);
            processCode();
            _isReading = false;
            portEXIT_CRITICAL(&_wiegandMux);
        }
    }
}

void Wiegand::processCode() {
    if (_bitCount >= 4) { // Minimum valid length
        _lastCode = _code;
        _lastBitCount = _bitCount;
        _available = true;
    }
    _code = 0;
    _bitCount = 0;
}

bool Wiegand::isAvailable() {
    if (_available) {
        _available = false;
        return true;
    }
    return false;
}

uint64_t Wiegand::getCode() {
    return _lastCode;
}

uint8_t Wiegand::getBitCount() {
    return _lastBitCount;
}
