#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include "config.h"

class ConfigManager {
public:
    ConfigManager();
    void begin();
    void load();
    void save();
    void factoryReset();
    
    // One-time code logic
    bool checkAndRedeemOneTimeCode(const char* scannedCode, String& labelOut);
    void resetDeliveryBlockIfNeeded(const char* requester);
    
    Config& getConfig() { return _config; }

private:
    Config _config;
};

extern ConfigManager configManager;

#endif
