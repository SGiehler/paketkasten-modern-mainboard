#ifndef ACCESS_CONTROL_H
#define ACCESS_CONTROL_H

#include <cstdint>
#include <string>

enum class AccessType {
    DENIED,
    OPEN_MAIL,
    OPEN_PARCEL
};

class AccessControl {
public:
    static bool matchesCode(const char* storedCode, const char* scannedCode, uint8_t bits = 26);
    static AccessType evaluate(const char* scannedCode, const char* ownerCodesJson, const char* deliveryCodesJson, std::string* labelOut = nullptr, uint8_t bits = 26);
};

#endif
