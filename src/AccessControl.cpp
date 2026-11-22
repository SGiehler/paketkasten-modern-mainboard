#include "AccessControl.h"
#include <ArduinoJson.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

static bool parseHexToDecString(const char* scannedCode, uint8_t bits, char* decOut, size_t decOutSize) {
    // Only non-keypad-burst Wiegand transfers (e.g. 26/34-bit) arrive as HEX strings.
    // 4-bit / 8-bit keypad bursts are already decimal PIN digits and must never be parsed as hex.
    if (bits == 4 || bits == 8 || !scannedCode || scannedCode[0] == '\0') {
        return false;
    }
    char* endptr = nullptr;
    unsigned long long val = strtoull(scannedCode, &endptr, 16);
    if (endptr != scannedCode && *endptr == '\0') {
        snprintf(decOut, decOutSize, "%llu", val);
        return true;
    }
    return false;
}

static bool matchesDecimalIgnoringLeadingZeros(const char* storedCode, const char* scannedDec) {
    if (!storedCode || storedCode[0] == '\0' || !scannedDec || scannedDec[0] == '\0') {
        return false;
    }
    // Only normalize if storedCode consists solely of decimal digits (0-9).
    for (const char* p = storedCode; *p != '\0'; ++p) {
        if (!isdigit(static_cast<unsigned char>(*p))) {
            return false;
        }
    }
    const char* stripped = storedCode;
    while (*stripped == '0' && *(stripped + 1) != '\0') {
        ++stripped;
    }
    return strcmp(stripped, scannedDec) == 0;
}

bool AccessControl::matchesCode(const char* storedCode, const char* scannedCode, uint8_t bits) {
    if (!storedCode || storedCode[0] == '\0' || !scannedCode || scannedCode[0] == '\0') {
        return false;
    }
    if (strcasecmp(storedCode, scannedCode) == 0) {
        return true;
    }
    char scannedCodeDec[32] = "";
    if (parseHexToDecString(scannedCode, bits, scannedCodeDec, sizeof(scannedCodeDec))) {
        if (strcmp(storedCode, scannedCodeDec) == 0) {
            return true;
        }
        if (matchesDecimalIgnoringLeadingZeros(storedCode, scannedCodeDec)) {
            return true;
        }
    }
    return false;
}

AccessType AccessControl::evaluate(const char* scannedCode, const char* ownerCodesJson, const char* deliveryCodesJson, std::string* labelOut, uint8_t bits) {
    if (!scannedCode || scannedCode[0] == '\0' || !ownerCodesJson || !deliveryCodesJson) {
        return AccessType::DENIED;
    }

    char scannedCodeDec[32] = "";
    const bool hasDecEquivalent = parseHexToDecString(scannedCode, bits, scannedCodeDec, sizeof(scannedCodeDec));

    auto codeMatches = [&](const char* objCode) -> bool {
        if (!objCode || objCode[0] == '\0') return false;
        if (strcasecmp(objCode, scannedCode) == 0) return true;
        if (hasDecEquivalent && (strcmp(objCode, scannedCodeDec) == 0 || matchesDecimalIgnoringLeadingZeros(objCode, scannedCodeDec))) {
            return true;
        }
        return false;
    };

    JsonDocument doc;
    
    // Check owner (mail) codes first
    DeserializationError error = deserializeJson(doc, ownerCodesJson);
    if (!error) {
        JsonArray array = doc.as<JsonArray>();
        for (JsonObject obj : array) {
            const char* objCode = obj["code"];
            if (codeMatches(objCode)) {
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
            if (codeMatches(objCode)) {
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
