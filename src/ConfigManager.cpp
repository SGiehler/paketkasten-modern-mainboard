#include "ConfigManager.h"
#include "state.h"
#include <Preferences.h>
#include <ArduinoJson.h>

Preferences preferences;
ConfigManager configManager;

ConfigManager::ConfigManager() {}

void ConfigManager::begin() {
    load();
}

void ConfigManager::load() {
    preferences.begin(PREFERENCES_NAMESPACE, false);
    _config.ssid = preferences.getString(SSID_KEY, "");
    _config.password = preferences.getString(PASSWORD_KEY, "");
    _config.ownerCodes = preferences.getString(OWNER_CODE_KEY, "[]");
    _config.deliveryCodes = preferences.getString(DELIVERY_CODE_KEY, "[]");
    _config.mqttServer = preferences.getString(MQTT_SERVER_KEY, "");
    _config.mqttPort = preferences.getInt(MQTT_PORT_KEY, 1883);
    _config.mqttUser = preferences.getString(MQTT_USER_KEY, "");
    _config.mqttPassword = preferences.getString(MQTT_PASSWORD_KEY, "");
    _config.dutyCycleOpen = preferences.getInt(DUTY_CYCLE_OPEN_KEY, 120);
    _config.dutyCycleClose = preferences.getInt(DUTY_CYCLE_CLOSE_KEY, 20);
    _config.selectedMelody = preferences.getString(SELECTED_MELODY_KEY, "NOKIA_TUNE");
    _config.callbackUrl = preferences.getString(CALLBACK_URL_KEY, "");
    _config.autolock = preferences.getBool(AUTOLOCK_KEY, true);
    _config.oneTimeCodes = preferences.getString(ONE_TIME_CODES_KEY, "[]");
    _config.oneTimeOpening = preferences.getBool(ONE_TIME_OPENING_KEY, false);
    _config.mqttUseTls = preferences.getBool(MQTT_USE_TLS_KEY, false);
    _config.mqttSkipCertVal = preferences.getBool(MQTT_SKIP_CERT_VAL_KEY, false);
    _config.callbackSkipCertVal = preferences.getBool(CALLBACK_SKIP_CERT_VAL_KEY, false);
    deliveryBlocked = preferences.getBool(DELIVERY_BLOCKED_KEY, false);
    preferences.end();
}

void ConfigManager::save() {
    if (!_config.oneTimeOpening) {
        deliveryBlocked = false;
    }
    preferences.begin(PREFERENCES_NAMESPACE, false);
    preferences.putString(SSID_KEY, _config.ssid);
    preferences.putString(PASSWORD_KEY, _config.password);
    preferences.putString(OWNER_CODE_KEY, _config.ownerCodes);
    preferences.putString(DELIVERY_CODE_KEY, _config.deliveryCodes);
    preferences.putString(MQTT_SERVER_KEY, _config.mqttServer);
    preferences.putInt(MQTT_PORT_KEY, _config.mqttPort);
    preferences.putString(MQTT_USER_KEY, _config.mqttUser);
    preferences.putString(MQTT_PASSWORD_KEY, _config.mqttPassword);
    preferences.putInt(DUTY_CYCLE_OPEN_KEY, _config.dutyCycleOpen);
    preferences.putInt(DUTY_CYCLE_CLOSE_KEY, _config.dutyCycleClose);
    preferences.putString(SELECTED_MELODY_KEY, _config.selectedMelody);
    preferences.putString(CALLBACK_URL_KEY, _config.callbackUrl);
    preferences.putBool(AUTOLOCK_KEY, _config.autolock);
    preferences.putString(ONE_TIME_CODES_KEY, _config.oneTimeCodes);
    preferences.putBool(ONE_TIME_OPENING_KEY, _config.oneTimeOpening);
    preferences.putBool(MQTT_USE_TLS_KEY, _config.mqttUseTls);
    preferences.putBool(MQTT_SKIP_CERT_VAL_KEY, _config.mqttSkipCertVal);
    preferences.putBool(CALLBACK_SKIP_CERT_VAL_KEY, _config.callbackSkipCertVal);
    preferences.putBool(DELIVERY_BLOCKED_KEY, deliveryBlocked);
    preferences.end();
}

void ConfigManager::factoryReset() {
    preferences.begin(PREFERENCES_NAMESPACE, false);
    preferences.clear();
    preferences.end();
    Serial.println("All preferences cleared.");
    load();
}

void ConfigManager::resetDeliveryBlockIfNeeded(const char* requester) {
    if (deliveryBlocked) {
        deliveryBlocked = false;
        preferences.begin(PREFERENCES_NAMESPACE, false);
        preferences.putBool(DELIVERY_BLOCKED_KEY, false);
        preferences.end();
        Serial.printf("Delivery block reset by owner (%s)\n", requester);
    }
}

bool ConfigManager::checkAndRedeemOneTimeCode(const char* scannedCode, String& labelOut) {
    if (_config.oneTimeCodes.length() == 0 || _config.oneTimeCodes == "[]") {
        return false;
    }
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, _config.oneTimeCodes);
    if (error) {
        Serial.println("Error parsing one-time codes JSON");
        return false;
    }

    // Convert scanned HEX code to its decimal representation
    char scannedCodeDec[32] = "";
    unsigned long val = strtoul(scannedCode, nullptr, 16);
    snprintf(scannedCodeDec, sizeof(scannedCodeDec), "%lu", val);

    JsonArray array = doc.as<JsonArray>();
    bool found = false;
    for (JsonObject obj : array) {
        const char* code = obj["code"];
        bool redeemed = obj["redeemed"] | false;
        if (code && (strcmp(code, scannedCode) == 0 || strcmp(code, scannedCodeDec) == 0)) {
            if (redeemed) {
                Serial.println("One-time code matched but already redeemed.");
                return false;
            }
            
            // Found an active code! Now check if we are delivery blocked.
            if (_config.oneTimeOpening && deliveryBlocked) {
                Serial.println("Access denied: One-time opening active and delivery blocked. Code NOT redeemed.");
                return false; // Keep code active!
            }
            
            labelOut = obj["label"] | "One-Time Code";
            obj["redeemed"] = true;
            obj["redeemedAt"] = millis(); // Store redemption timestamp
            found = true;
            break;
        }
    }
    if (found) {
        String updatedJson;
        serializeJson(doc, updatedJson);
        _config.oneTimeCodes = updatedJson;
        
        preferences.begin(PREFERENCES_NAMESPACE, false);
        preferences.putString(ONE_TIME_CODES_KEY, _config.oneTimeCodes);
        preferences.end();
        Serial.printf("One-time code redeemed: %s\n", labelOut.c_str());
        return true;
    }
    return false;
}
