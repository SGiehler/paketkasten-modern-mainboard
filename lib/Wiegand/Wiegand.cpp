#include "Wiegand.h"

Wiegand* Wiegand::instance = nullptr;

Wiegand::Wiegand() {
    instance = this;
    _bitCount = 0;
    _code = 0;
    _isReading = false;
    _lastWakeMicros = 0;
    _available = false;
    _lastCode = 0;
    _lastBitCount = 0;
    _taskHandle = NULL;
}

void Wiegand::begin(uint8_t pinD0, uint8_t pinD1) {
    _pinD0 = pinD0;
    _pinD1 = pinD1;

    pinMode(_pinD0, INPUT_PULLUP);
    pinMode(_pinD1, INPUT_PULLUP);

    // Store the calling task's handle for ISR notification
    _taskHandle = xTaskGetCurrentTaskHandle();

    attach();
}

void Wiegand::attach() {
    _bitCount = 0;
    _code = 0;
    _isReading = false;
    _lastWakeMicros = 0;

    attachInterrupt(digitalPinToInterrupt(_pinD0), data0ISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(_pinD1), data1ISR, FALLING);
}

void Wiegand::detach() {
    detachInterrupt(digitalPinToInterrupt(_pinD0));
    detachInterrupt(digitalPinToInterrupt(_pinD1));
}

// ISR: wake-up trigger ONLY — no bit counting!
// The polling loop in loop() captures ALL bits to avoid transition bugs.
void IRAM_ATTR Wiegand::data0ISR() {
    if (!instance->_isReading) {
        instance->_isReading = true;
        // Wake the Wiegand task immediately
        if (instance->_taskHandle != NULL) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            vTaskNotifyGiveFromISR(instance->_taskHandle, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }
}

void IRAM_ATTR Wiegand::data1ISR() {
    if (!instance->_isReading) {
        instance->_isReading = true;
        // Wake the Wiegand task immediately
        if (instance->_taskHandle != NULL) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            vTaskNotifyGiveFromISR(instance->_taskHandle, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    }
}

void Wiegand::loop() {
    if (!_isReading) {
        return;
    }

    // Detach interrupts — we're polling now, don't want ISRs firing during capture
    detachInterrupt(digitalPinToInterrupt(_pinD0));
    detachInterrupt(digitalPinToInterrupt(_pinD1));

    // Reset for fresh capture — ISR did NOT count any bits
    _bitCount = 0;
    _code = 0;

    bool d0_active = false;
    bool d1_active = false;

    // The first pulse that triggered the ISR may still be LOW on the wire.
    // Detect and capture it right now.
    if (digitalRead(_pinD0) == LOW) {
        _code <<= 1;       // D0 = bit 0
        _bitCount++;
        d0_active = true;
        // Wait for pulse to end
        unsigned long pulseStart = millis();
        while (digitalRead(_pinD0) == LOW && (millis() - pulseStart < 80)) {}
        delayMicroseconds(500); // blind time to kill bouncing
    } else if (digitalRead(_pinD1) == LOW) {
        _code <<= 1;
        _code |= 1;        // D1 = bit 1
        _bitCount++;
        d1_active = true;
        unsigned long pulseStart = millis();
        while (digitalRead(_pinD1) == LOW && (millis() - pulseStart < 80)) {}
        delayMicroseconds(500);
    } else {
        // Pulse already ended before we got here — count it as a D0 or D1?
        // We don't know which line triggered, but the ISR fired on FALLING edge.
        // Since we can't tell, we missed it. But this is extremely unlikely
        // given vTaskNotifyGiveFromISR wakes us within microseconds.
        // If we got here, the pulse was < ~10µs which is too short for valid Wiegand.
    }

    // Poll for remaining bits with 80ms silence timeout
    unsigned long lastPulseTime = millis();
    while (millis() - lastPulseTime < 80) {
        bool d0 = (digitalRead(_pinD0) == LOW);
        bool d1 = (digitalRead(_pinD1) == LOW);

        if (d0 && !d0_active) {
            if (_bitCount < 32) {
                _code <<= 1;
                _bitCount++;
            }
            lastPulseTime = millis();
            // Block until the pulse ends to debounce
            while (digitalRead(_pinD0) == LOW && (millis() - lastPulseTime < 80)) {}
            delayMicroseconds(500);
            d0_active = true;
        } else if (!d0) {
            d0_active = false;
        }

        if (d1 && !d1_active) {
            if (_bitCount < 32) {
                _code <<= 1;
                _code |= 1;
                _bitCount++;
            }
            lastPulseTime = millis();
            while (digitalRead(_pinD1) == LOW && (millis() - lastPulseTime < 80)) {}
            delayMicroseconds(500);
            d1_active = true;
        } else if (!d1) {
            d1_active = false;
        }
    }

    // Sequence finished
    processCode();
    _isReading = false;

    // Re-attach interrupts for the next card scan
    attachInterrupt(digitalPinToInterrupt(_pinD0), data0ISR, FALLING);
    attachInterrupt(digitalPinToInterrupt(_pinD1), data1ISR, FALLING);
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
