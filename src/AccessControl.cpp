#include "AccessControl.h"
#include <ArduinoJson.h>
#include <cstring>

AccessType AccessControl::evaluate(const char* scannedCode, const char* ownerCodesJson, const char* deliveryCodesJson, std::string* labelOut) {
    if (!scannedCode || !ownerCodesJson || !deliveryCodesJson) {
        return AccessType::DENIED;
    }

    JsonDocument doc;
    
    // Check owner (mail) codes first
    DeserializationError error = deserializeJson(doc, ownerCodesJson);
    if (!error) {
        JsonArray array = doc.as<JsonArray>();
        for (JsonObject obj : array) {
            const char* objCode = obj["code"];
            if (objCode && strcmp(objCode, scannedCode) == 0) {
                if (labelOut) {
                    const char* label = obj["label"];
                    *labelOut = label ? label : "unknown";
                }
                return AccessType::OPEN_MAIL;
            }
        }
    }

    doc.clear();
    
    // Check delivery (parcel) codes
    error = deserializeJson(doc, deliveryCodesJson);
    if (!error) {
        JsonArray array = doc.as<JsonArray>();
        for (JsonObject obj : array) {
            const char* objCode = obj["code"];
            if (objCode && strcmp(objCode, scannedCode) == 0) {
                if (labelOut) {
                    const char* label = obj["label"];
                    *labelOut = label ? label : "unknown";
                }
                return AccessType::OPEN_PARCEL;
            }
        }
    }

    return AccessType::DENIED;
}
