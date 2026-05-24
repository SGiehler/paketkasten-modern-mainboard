#include "WiegandManager.h"
#include "state.h"

WiegandManager wiegandManager(WIEGAND_D0_PIN, WIEGAND_D1_PIN);

WiegandManager::WiegandManager(int d0Pin, int d1Pin) :
    _d0Pin(d0Pin),
    _d1Pin(d1Pin),
    _attached(true),
    _onCodeCallback(nullptr)
{}

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
    _wiegand.loop();
    if (_wiegand.isAvailable()) {
        char codeStr[12];
        snprintf(codeStr, sizeof(codeStr), "%lX", _wiegand.getCode());
        if (_onCodeCallback) {
            _onCodeCallback(codeStr, 26); // 26 bits default format
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
