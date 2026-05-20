#ifndef ACCESS_CONTROL_H
#define ACCESS_CONTROL_H

#include <string>

enum class AccessType {
    DENIED,
    OPEN_MAIL,
    OPEN_PARCEL
};

class AccessControl {
public:
    static AccessType evaluate(const char* scannedCode, const char* ownerCodesJson, const char* deliveryCodesJson, std::string* labelOut = nullptr);
};

#endif
