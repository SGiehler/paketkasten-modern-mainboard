#include "Wiegand.h"

Wiegand* Wiegand::instance = nullptr;

Wiegand::Wiegand() {
    instance = this;
    _bitCount = 0;
    _code = 0;
    _lastBitTime = 0;
    _available = false;
    _lastCode = 0;
    _lastBitCount = 0;
}

void Wiegand::begin(uint8_t pinD0, uint8_t pinD1) {
    _pinD0 = pinD0;
    _pinD1 = pinD1;

    pinMode(_pinD0, INPUT_PULLUP);
    pinMode(_pinD1, INPUT_PULLUP);

    attach();
}

void Wiegand::attach() {
    attachInterrupt(digitalPinToInterrupt(_pinD0), data0ISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(_pinD1), data1ISR, FALLING);
}

void Wiegand::detach() {
    detachInterrupt(digitalPinToInterrupt(_pinD0));
    detachInterrupt(digitalPinToInterrupt(_pinD1));
}

void IRAM_ATTR Wiegand::data0ISR() {
    instance->handleData(0);
}

void IRAM_ATTR Wiegand::data1ISR() {
    instance->handleData(1);
}

void Wiegand::handleData(uint8_t data) {
    if (instance->_bitCount < 32) {
        instance->_code <<= 1;
        if (data) {
            instance->_code |= 1;
        }
    }
    instance->_bitCount += 1;
    instance->_lastBitTime = millis();
}

void Wiegand::loop() {
    if (_bitCount > 0 && (millis() - _lastBitTime) > 50) {
        processCode();
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

uint32_t Wiegand::getCode() {
    return _lastCode;
}

uint8_t Wiegand::getBitCount() {
    return _lastBitCount;
}
