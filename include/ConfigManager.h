#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "config.h"
#include <functional>

struct MotorSettings {
    int motorKickMs;
    int dutyCycleOpen;
    int dutyCycleClose;
    bool autolock;
    bool oneTimeOpening;
    bool devMode;
    bool hasMailCompartment;
};

class ConfigManager {
public:
    ConfigManager();
    void begin();
    void load();
    void save();
    void factoryReset();

    // Thread-safe accessors
    MotorSettings getMotorSettings();
    Config getConfigCopy();
    bool hasMqttServer();
    String getOneTimeCodesCopy();
    void setOneTimeCodes(const String& json);
    void setDevMode(bool enabled);
    void updateConfig(const std::function<void(Config&)>& mutator, bool saveToNvs = true);

    // One-time code & delivery block logic
    bool checkAndRedeemOneTimeCode(const char* scannedCode, String& labelOut, uint8_t bits = 26);
    void resetDeliveryBlockIfNeeded(const char* requester);
    void saveDeliveryBlocked(bool blocked);

    // Direct reference kept only for single-threaded setup paths; prefer thread-safe helpers above.
    Config& getConfig() { return _config; }

private:
    void lock();
    void unlock();
    void saveUnlocked();
    void loadUnlocked();

    Config _config;
    SemaphoreHandle_t _mutex;
};

extern ConfigManager configManager;

#endif
