#include "WiegandManager.h"
#include "state.h"

WiegandManager wiegandManager(WIEGAND_D0_PIN, WIEGAND_D1_PIN);

WiegandManager::WiegandManager(int d0Pin, int d1Pin) :
    _d0Pin(d0Pin),
    _d1Pin(d1Pin),
    _attached(true),
    _onCodeCallback(nullptr),
    _keypadPinLen(0),
    _lastKeypadPressTime(0)
{
    _keypadPinStr[0] = '\0';
}

void WiegandManager::begin(void (*onCodeCallback)(char* code, uint8_t bits)) {
    _onCodeCallback = onCodeCallback;
    
    // Dedicated task pinned to Core 1 (APP_CPU), polls every 10ms
    xTaskCreatePinnedToCore(
        [](void* arg) {
            WiegandManager* manager = static_cast<WiegandManager*>(arg);
            manager->_wiegand.begin(manager->_d0Pin, manager->_d1Pin);
            for (;;) {
                manager->update();
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        },
        "WiegandTask",
        4096,             // Stack size
        this,             // Pass this pointer
        2,                // Priority (higher than appTask)
        NULL,             // Task handle
        1                 // Core 1 (APP_CPU)
    );
}

void WiegandManager::update() {
    if (currentState != LOCKED) {
        return;
    }

    // Clear buffer after 10 seconds of inactivity
    if (_keypadPinLen > 0 && (millis() - _lastKeypadPressTime > 10000)) {
        _keypadPinLen = 0;
        _keypadPinStr[0] = '\0';
        Serial.println("Wiegand keypad PIN buffer cleared due to timeout.");
    }

    _wiegand.loop();
    if (_wiegand.isAvailable()) {
        uint64_t rawCode = _wiegand.getCode();
        uint8_t bitCount = _wiegand.getBitCount();

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

            if (key != 0xFF) {
                if (key < 10) {
                    if (_keypadPinLen < sizeof(_keypadPinStr) - 1) {
                        _keypadPinStr[_keypadPinLen++] = '0' + key;
                        _keypadPinStr[_keypadPinLen] = '\0';
                        _lastKeypadPressTime = millis();
                        Serial.printf("Keypad digit: %d, current PIN: %s\n", key, _keypadPinStr);
                    }
                } else {
                    // Termination key (* or #)
                    if (_keypadPinLen > 0) {
                        Serial.printf("Keypad PIN complete: %s\n", _keypadPinStr);
                        if (_onCodeCallback) {
                            _onCodeCallback(_keypadPinStr, bitCount);
                        }
                        _keypadPinLen = 0;
                        _keypadPinStr[0] = '\0';
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
    _attached = false;
}
