#include "WiegandManager.h"
#include "TelemetryLog.h"
#include "state.h"
#include <cstring>

WiegandManager wiegandManager(WIEGAND_D0_PIN, WIEGAND_D1_PIN);

static int checkWiegandParity(uint64_t rawCode, uint8_t bitCount) {
    // Returns: 1 = parity valid, 0 = parity invalid, -1 = not applicable
    if (bitCount != 26 && bitCount != 34) {
        return -1;
    }
    const uint8_t half = (bitCount - 2) / 2; // 12 for 26-bit, 16 for 34-bit
    uint8_t leadingParity = (rawCode >> (bitCount - 1)) & 1U;
    uint8_t trailingParity = rawCode & 1U;

    uint8_t upperOnes = 0;
    for (uint8_t i = 0; i < half; ++i) {
        if ((rawCode >> (1 + half + i)) & 1ULL) upperOnes++;
    }
    uint8_t lowerOnes = 0;
    for (uint8_t i = 0; i < half; ++i) {
        if ((rawCode >> (1 + i)) & 1ULL) lowerOnes++;
    }

    bool evenOk = ((upperOnes + leadingParity) % 2) == 0;
    bool oddOk = ((lowerOnes + trailingParity) % 2) == 1;
    return (evenOk && oddOk) ? 1 : 0;
}

WiegandManager::WiegandManager(int d0Pin, int d1Pin) :
    _d0Pin(d0Pin),
    _d1Pin(d1Pin),
    _attached(false),
    _onCodeCallback(nullptr),
    _pinBufMux(portMUX_INITIALIZER_UNLOCKED),
    _keypadPinLen(0),
    _lastKeypadPressTime(0)
{
    memset(&_lastStats, 0, sizeof(_lastStats));
    _keypadPinStr[0] = '\0';
}

WiegandFrameStats WiegandManager::getLastFrameStats() {
    portENTER_CRITICAL(&_pinBufMux);
    WiegandFrameStats copy = _lastStats;
    portEXIT_CRITICAL(&_pinBufMux);
    return copy;
}

void WiegandManager::begin(void (*onCodeCallback)(char* code, uint8_t bits)) {
    _onCodeCallback = onCodeCallback;

    // Initialize pins and interrupts synchronously before starting tasks so detach()
    // can never operate on uninitialized pin numbers.
    _wiegand.begin(_d0Pin, _d1Pin);
    _attached = true;

    // Dedicated task pinned to Core 1 (APP_CPU), polls every 5ms for snappy frame completion
    xTaskCreatePinnedToCore(
        [](void* arg) {
            WiegandManager* manager = static_cast<WiegandManager*>(arg);
            for (;;) {
                manager->update();
                vTaskDelay(pdMS_TO_TICKS(5));
            }
        },
        "WiegandTask",
        4096,             // Stack size (callbacks are queued asynchronously, no TLS on this stack)
        this,             // Pass this pointer
        2,                // Priority (higher than appTask)
        NULL,             // Task handle
        1                 // Core 1 (APP_CPU)
    );
}

void WiegandManager::update() {
    if (currentState != LOCKED || otaInProgress) {
        return;
    }

    // Clear partial keypad PIN buffer after 10 seconds of inactivity
    bool timedOut = false;
    portENTER_CRITICAL(&_pinBufMux);
    if (_keypadPinLen > 0 && (millis() - _lastKeypadPressTime > 10000)) {
        _keypadPinLen = 0;
        _keypadPinStr[0] = '\0';
        timedOut = true;
    }
    portEXIT_CRITICAL(&_pinBufMux);
    if (timedOut) {
        telemetryLog.logf("WIEGAND", "Keypad PIN buffer cleared (10s timeout)");
    }

    _wiegand.loop();
    if (_wiegand.isAvailable()) {
        WiegandFrameStats stats = _wiegand.getFrameStats();
        portENTER_CRITICAL(&_pinBufMux);
        _lastStats = stats;
        portEXIT_CRITICAL(&_pinBufMux);

        const uint64_t rawCode = stats.rawCode;
        const uint8_t bitCount = stats.bitCount;

        if (bitCount < 4) {
            telemetryLog.logf("WIEGAND", "Noise/short frame ignored: bits=%u raw=0x%llX gap=%u..%uus rej=%u",
                              bitCount, (unsigned long long)rawCode,
                              stats.minBitGapUs, stats.maxBitGapUs, stats.rejectedPulses);
            return;
        }

        if (bitCount == 4 || bitCount == 8) {
            uint8_t key = 0xFF;
            if (bitCount == 4) {
                key = rawCode & 0x0F;
            } else { // bitCount == 8
                uint8_t lowNibble = rawCode & 0x0F;
                uint8_t highNibble = (rawCode & 0xF0) >> 4;
                if (lowNibble == ((~highNibble) & 0x0F)) {
                    key = lowNibble;
                }
            }

            if (key == 0xFF) {
                telemetryLog.logf("WIEGAND", "Invalid 8-bit key complement: raw=0x%llX gap=%u..%uus rej=%u",
                                  (unsigned long long)rawCode, stats.minBitGapUs, stats.maxBitGapUs, stats.rejectedPulses);
                return;
            }

            if (key < 10) {
                uint8_t newLen = 0;
                portENTER_CRITICAL(&_pinBufMux);
                if (_keypadPinLen < sizeof(_keypadPinStr) - 1) {
                    _keypadPinStr[_keypadPinLen++] = '0' + key;
                    _keypadPinStr[_keypadPinLen] = '\0';
                    _lastKeypadPressTime = millis();
                    newLen = _keypadPinLen;
                }
                portEXIT_CRITICAL(&_pinBufMux);
                telemetryLog.logf("WIEGAND", "Keypad digit buffered (bits=%u, len=%u, gap=%u..%uus, rej=%u)",
                                  bitCount, newLen, stats.minBitGapUs, stats.maxBitGapUs, stats.rejectedPulses);
            } else if (key == 10) {
                // Standard Wiegand 0x0A = '*' (ESC / Clear)
                portENTER_CRITICAL(&_pinBufMux);
                _keypadPinLen = 0;
                _keypadPinStr[0] = '\0';
                portEXIT_CRITICAL(&_pinBufMux);
                telemetryLog.logf("WIEGAND", "Keypad PIN cleared (* pressed)");
            } else {
                // Standard Wiegand 0x0B = '#' (ENT / Submit)
                char completedPin[sizeof(_keypadPinStr)] = "";
                bool hasPin = false;
                portENTER_CRITICAL(&_pinBufMux);
                if (_keypadPinLen > 0) {
                    strncpy(completedPin, _keypadPinStr, sizeof(completedPin) - 1);
                    completedPin[sizeof(completedPin) - 1] = '\0';
                    _keypadPinLen = 0;
                    _keypadPinStr[0] = '\0';
                    hasPin = true;
                }
                portEXIT_CRITICAL(&_pinBufMux);

                if (hasPin) {
                    telemetryLog.logf("WIEGAND", "Keypad PIN submitted (len=%u, bits=%u)",
                                      (unsigned)strlen(completedPin), bitCount);
                    if (_onCodeCallback) {
                        _onCodeCallback(completedPin, bitCount);
                    }
                }
            }
            return;
        }

        uint64_t processedCode = rawCode;
        if (bitCount == 26) {
            processedCode = (rawCode >> 1) & 0xFFFFFF; // Extract full 24-bit data (no 16-bit truncation)
        } else if (bitCount == 34) {
            processedCode = (rawCode >> 1) & 0xFFFFFFFF; // Extract full 32-bit data
        }

        char codeStr[20];
        snprintf(codeStr, sizeof(codeStr), "%llX", (unsigned long long)processedCode);

        int parity = checkWiegandParity(rawCode, bitCount);
        const char* parityStr = (parity == 1) ? "OK" : (parity == 0 ? "BAD" : "N/A");
        telemetryLog.logf("WIEGAND", "Frame bits=%u hex=%s raw=0x%llX parity=%s gap=%u..%uus dur=%ums rej=%u",
                          bitCount, codeStr, (unsigned long long)rawCode, parityStr,
                          stats.minBitGapUs, stats.maxBitGapUs, stats.durationMs, stats.rejectedPulses);

        if (_onCodeCallback) {
            _onCodeCallback(codeStr, bitCount);
        }
    }
}

void WiegandManager::attach() {
    _wiegand.attach();
    _attached = true;
}

void WiegandManager::detach() {
    _wiegand.detach();
    portENTER_CRITICAL(&_pinBufMux);
    _keypadPinLen = 0;
    _keypadPinStr[0] = '\0';
    portEXIT_CRITICAL(&_pinBufMux);
    _attached = false;
}
