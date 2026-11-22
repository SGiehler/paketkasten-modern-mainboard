#include "ConfigManager.h"
#include "AccessControl.h"
#include "MotorDriveProfile.h"
#include "TelemetryLog.h"
#include "state.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

static Preferences preferences;
ConfigManager configManager;

static void warnIfPutFailed(const char* key, size_t written, size_t expected) {
    if (expected > 0 && written == 0) {
        telemetryLog.logf("CONFIG", "NVS putString('%s') failed (len=%u)", key, (unsigned)expected);
    }
}

ConfigManager::ConfigManager() : _mutex(nullptr) {}

void ConfigManager::lock() {
    if (_mutex != nullptr) {
        xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
    }
}

void ConfigManager::unlock() {
    if (_mutex != nullptr) {
        xSemaphoreGiveRecursive(_mutex);
    }
}

void ConfigManager::begin() {
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateRecursiveMutex();
    }
    load();
}

void ConfigManager::load() {
    lock();
    loadUnlocked();
    unlock();
}

void ConfigManager::loadUnlocked() {
    preferences.begin(PREFERENCES_NAMESPACE, false);
    _config.ssid = preferences.getString(SSID_KEY, "");
    _config.password = preferences.getString(PASSWORD_KEY, "");
    _config.ownerCodes = preferences.getString(OWNER_CODE_KEY, "[]");
    _config.deliveryCodes = preferences.getString(DELIVERY_CODE_KEY, "[]");
    _config.mqttServer = preferences.getString(MQTT_SERVER_KEY, "");
    _config.mqttPort = preferences.getInt(MQTT_PORT_KEY, 1883);
    if (_config.mqttPort <= 0 || _config.mqttPort > 65535) {
        _config.mqttPort = 1883;
    }
    _config.mqttUser = preferences.getString(MQTT_USER_KEY, "");
    _config.mqttPassword = preferences.getString(MQTT_PASSWORD_KEY, "");
    _config.dutyCycleOpen = preferences.getInt(DUTY_CYCLE_OPEN_KEY, MotorProfile::OPEN_DUTY_DEFAULT);
    if (_config.dutyCycleOpen < MotorProfile::OPEN_DUTY_MIN || _config.dutyCycleOpen > MotorProfile::OPEN_DUTY_MAX) {
        _config.dutyCycleOpen = MotorProfile::OPEN_DUTY_DEFAULT;
    }
    _config.dutyCycleClose = preferences.getInt(DUTY_CYCLE_CLOSE_KEY, MotorProfile::CLOSE_DUTY_DEFAULT);
    if (_config.dutyCycleClose < MotorProfile::CLOSE_DUTY_MIN || _config.dutyCycleClose > MotorProfile::CLOSE_DUTY_MAX) {
        _config.dutyCycleClose = MotorProfile::CLOSE_DUTY_DEFAULT;
    }
    _config.motorKickMs = preferences.getInt(MOTOR_KICK_MS_KEY, MotorProfile::KICK_MS_DEFAULT);
    if (_config.motorKickMs < MotorProfile::KICK_MS_MIN || _config.motorKickMs > MotorProfile::KICK_MS_MAX) {
        _config.motorKickMs = MotorProfile::KICK_MS_DEFAULT;
    }
    _config.selectedMelody = preferences.getString(SELECTED_MELODY_KEY, "NOKIA_TUNE");
    _config.callbackUrl = preferences.getString(CALLBACK_URL_KEY, "");
    _config.autolock = preferences.getBool(AUTOLOCK_KEY, true);
    _config.oneTimeCodes = preferences.getString(ONE_TIME_CODES_KEY, "[]");
    _config.oneTimeOpening = preferences.getBool(ONE_TIME_OPENING_KEY, false);
    _config.mqttUseTls = preferences.getBool(MQTT_USE_TLS_KEY, false);
    _config.mqttSkipCertVal = preferences.getBool(MQTT_SKIP_CERT_VAL_KEY, false);
    _config.callbackSkipCertVal = preferences.getBool(CALLBACK_SKIP_CERT_VAL_KEY, false);
    _config.devMode = preferences.getBool(DEV_MODE_KEY, false);
    _config.hasMailCompartment = preferences.getBool(HAS_MAIL_KEY, true);
    deliveryBlocked = preferences.getBool(DELIVERY_BLOCKED_KEY, false);
    preferences.end();
}

void ConfigManager::save() {
    lock();
    saveUnlocked();
    unlock();
}

void ConfigManager::saveUnlocked() {
    if (!_config.oneTimeOpening) {
        deliveryBlocked = false;
    }
    preferences.begin(PREFERENCES_NAMESPACE, false);
    warnIfPutFailed(SSID_KEY, preferences.putString(SSID_KEY, _config.ssid), _config.ssid.length());
    warnIfPutFailed(PASSWORD_KEY, preferences.putString(PASSWORD_KEY, _config.password), _config.password.length());
    warnIfPutFailed(OWNER_CODE_KEY, preferences.putString(OWNER_CODE_KEY, _config.ownerCodes), _config.ownerCodes.length());
    warnIfPutFailed(DELIVERY_CODE_KEY, preferences.putString(DELIVERY_CODE_KEY, _config.deliveryCodes), _config.deliveryCodes.length());
    warnIfPutFailed(MQTT_SERVER_KEY, preferences.putString(MQTT_SERVER_KEY, _config.mqttServer), _config.mqttServer.length());
    preferences.putInt(MQTT_PORT_KEY, _config.mqttPort);
    warnIfPutFailed(MQTT_USER_KEY, preferences.putString(MQTT_USER_KEY, _config.mqttUser), _config.mqttUser.length());
    warnIfPutFailed(MQTT_PASSWORD_KEY, preferences.putString(MQTT_PASSWORD_KEY, _config.mqttPassword), _config.mqttPassword.length());
    preferences.putInt(DUTY_CYCLE_OPEN_KEY, _config.dutyCycleOpen);
    preferences.putInt(DUTY_CYCLE_CLOSE_KEY, _config.dutyCycleClose);
    preferences.putInt(MOTOR_KICK_MS_KEY, _config.motorKickMs);
    warnIfPutFailed(SELECTED_MELODY_KEY, preferences.putString(SELECTED_MELODY_KEY, _config.selectedMelody), _config.selectedMelody.length());
    warnIfPutFailed(CALLBACK_URL_KEY, preferences.putString(CALLBACK_URL_KEY, _config.callbackUrl), _config.callbackUrl.length());
    preferences.putBool(AUTOLOCK_KEY, _config.autolock);
    warnIfPutFailed(ONE_TIME_CODES_KEY, preferences.putString(ONE_TIME_CODES_KEY, _config.oneTimeCodes), _config.oneTimeCodes.length());
    preferences.putBool(ONE_TIME_OPENING_KEY, _config.oneTimeOpening);
    preferences.putBool(MQTT_USE_TLS_KEY, _config.mqttUseTls);
    preferences.putBool(MQTT_SKIP_CERT_VAL_KEY, _config.mqttSkipCertVal);
    preferences.putBool(CALLBACK_SKIP_CERT_VAL_KEY, _config.callbackSkipCertVal);
    preferences.putBool(DEV_MODE_KEY, _config.devMode);
    preferences.putBool(HAS_MAIL_KEY, _config.hasMailCompartment);
    preferences.putBool(DELIVERY_BLOCKED_KEY, deliveryBlocked);
    preferences.end();
}

void ConfigManager::factoryReset() {
    lock();
    preferences.begin(PREFERENCES_NAMESPACE, false);
    preferences.clear();
    preferences.end();
    if (LittleFS.exists("/certs/mqtt_ca.pem")) {
        LittleFS.remove("/certs/mqtt_ca.pem");
    }
    if (LittleFS.exists("/certs/callback_ca.pem")) {
        LittleFS.remove("/certs/callback_ca.pem");
    }
    telemetryLog.logf("CONFIG", "Factory reset: preferences & TLS certs cleared");
    loadUnlocked();
    unlock();
}

MotorSettings ConfigManager::getMotorSettings() {
    lock();
    MotorSettings s = {
        _config.motorKickMs,
        _config.dutyCycleOpen,
        _config.dutyCycleClose,
        _config.autolock,
        _config.oneTimeOpening,
        _config.devMode,
        _config.hasMailCompartment
    };
    unlock();
    return s;
}

Config ConfigManager::getConfigCopy() {
    lock();
    Config copy = _config;
    unlock();
    return copy;
}

bool ConfigManager::hasMqttServer() {
    lock();
    bool has = (_config.mqttServer.length() > 0);
    unlock();
    return has;
}

String ConfigManager::getOneTimeCodesCopy() {
    lock();
    String copy = _config.oneTimeCodes;
    unlock();
    return copy;
}

void ConfigManager::setOneTimeCodes(const String& json) {
    lock();
    _config.oneTimeCodes = json;
    preferences.begin(PREFERENCES_NAMESPACE, false);
    warnIfPutFailed(ONE_TIME_CODES_KEY, preferences.putString(ONE_TIME_CODES_KEY, _config.oneTimeCodes), _config.oneTimeCodes.length());
    preferences.end();
    unlock();
}

void ConfigManager::setDevMode(bool enabled) {
    lock();
    _config.devMode = enabled;
    preferences.begin(PREFERENCES_NAMESPACE, false);
    preferences.putBool(DEV_MODE_KEY, enabled);
    preferences.end();
    unlock();
    telemetryLog.logf("SYS", "Dev telemetry mode %s", enabled ? "enabled" : "disabled");
}

void ConfigManager::updateConfig(const std::function<void(Config&)>& mutator, bool saveToNvs) {
    lock();
    mutator(_config);
    if (saveToNvs) {
        saveUnlocked();
    }
    unlock();
}

void ConfigManager::saveDeliveryBlocked(bool blocked) {
    lock();
    deliveryBlocked = blocked;
    preferences.begin(PREFERENCES_NAMESPACE, false);
    preferences.putBool(DELIVERY_BLOCKED_KEY, blocked);
    preferences.end();
    unlock();
}

void ConfigManager::resetDeliveryBlockIfNeeded(const char* requester) {
    lock();
    if (deliveryBlocked) {
        deliveryBlocked = false;
        preferences.begin(PREFERENCES_NAMESPACE, false);
        preferences.putBool(DELIVERY_BLOCKED_KEY, false);
        preferences.end();
        telemetryLog.logf("ACCESS", "Delivery block reset by %s", requester);
    }
    unlock();
}

bool ConfigManager::checkAndRedeemOneTimeCode(const char* scannedCode, String& labelOut, uint8_t bits) {
    if (!scannedCode || scannedCode[0] == '\0') {
        return false;
    }

    lock();
    if (_config.oneTimeCodes.length() == 0 || _config.oneTimeCodes == "[]") {
        unlock();
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _config.oneTimeCodes);
    if (error) {
        telemetryLog.logf("ACCESS", "Error parsing one-time codes JSON");
        unlock();
        return false;
    }

    JsonArray array = doc.as<JsonArray>();
    bool found = false;
    for (JsonObject obj : array) {
        const char* code = obj["code"];
        bool redeemed = obj["redeemed"] | false;
        if (AccessControl::matchesCode(code, scannedCode, bits)) {
            if (redeemed) {
                telemetryLog.logf("ACCESS", "One-time code already redeemed");
                unlock();
                return false;
            }
            
            if (_config.oneTimeOpening && deliveryBlocked) {
                telemetryLog.logf("ACCESS", "OTC denied: delivery blocked (kept active)");
                unlock();
                return false;
            }
            
            labelOut = obj["label"] | "One-Time Code";
            obj["redeemed"] = true;
            obj["redeemedAt"] = millis();
            found = true;
            break;
        }
    }

    if (found) {
        String updatedJson;
        serializeJson(doc, updatedJson);
        _config.oneTimeCodes = updatedJson;
        
        preferences.begin(PREFERENCES_NAMESPACE, false);
        warnIfPutFailed(ONE_TIME_CODES_KEY, preferences.putString(ONE_TIME_CODES_KEY, _config.oneTimeCodes), _config.oneTimeCodes.length());
        preferences.end();
        telemetryLog.logf("ACCESS", "One-time code redeemed: %s", labelOut.c_str());
        unlock();
        return true;
    }
    unlock();
    return false;
}
