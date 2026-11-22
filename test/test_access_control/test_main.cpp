#include <unity.h>
#include "AccessControl.h"

void setUp(void) {
    // set stuff up here
}

void tearDown(void) {
    // clean stuff up here
}

void test_access_denied_empty_json(void) {
    AccessType result = AccessControl::evaluate("12345", "[]", "[]");
    TEST_ASSERT_EQUAL(static_cast<int>(AccessType::DENIED), static_cast<int>(result));
}

void test_access_open_mail_owner(void) {
    const char* ownerJson = "[{\"code\":\"ABCDEF\",\"label\":\"User1\"}]";
    const char* deliveryJson = "[]";
    std::string label;
    AccessType result = AccessControl::evaluate("ABCDEF", ownerJson, deliveryJson, &label);
    
    TEST_ASSERT_EQUAL(static_cast<int>(AccessType::OPEN_MAIL), static_cast<int>(result));
    TEST_ASSERT_EQUAL_STRING("User1", label.c_str());
}

void test_access_open_parcel_delivery(void) {
    const char* ownerJson = "[{\"code\":\"ABCDEF\",\"label\":\"User1\"}]";
    const char* deliveryJson = "[{\"code\":\"987654\",\"label\":\"DHL\"}]";
    std::string label;
    AccessType result = AccessControl::evaluate("987654", ownerJson, deliveryJson, &label);
    
    TEST_ASSERT_EQUAL(static_cast<int>(AccessType::OPEN_PARCEL), static_cast<int>(result));
    TEST_ASSERT_EQUAL_STRING("DHL", label.c_str());
}

void test_access_denied_wrong_code(void) {
    const char* ownerJson = "[{\"code\":\"ABCDEF\",\"label\":\"User1\"}]";
    const char* deliveryJson = "[{\"code\":\"987654\",\"label\":\"DHL\"}]";
    AccessType result = AccessControl::evaluate("111111", ownerJson, deliveryJson);
    
    TEST_ASSERT_EQUAL(static_cast<int>(AccessType::DENIED), static_cast<int>(result));
}

void test_access_handles_broken_json(void) {
    const char* brokenJson = "{\"code\":";
    AccessType result = AccessControl::evaluate("ABCDEF", brokenJson, brokenJson);
    TEST_ASSERT_EQUAL(static_cast<int>(AccessType::DENIED), static_cast<int>(result));
}

void test_access_handles_missing_label(void) {
    const char* ownerJson = "[{\"code\":\"NOLABEL\"}]";
    std::string label;
    AccessType result = AccessControl::evaluate("NOLABEL", ownerJson, "[]", &label);
    
    TEST_ASSERT_EQUAL(static_cast<int>(AccessType::OPEN_MAIL), static_cast<int>(result));
    TEST_ASSERT_EQUAL_STRING("unknown", label.c_str());
}

void test_access_decimal_stored_matches_scanned_hex(void) {
    const char* ownerJson = "[{\"code\":\"123456\",\"label\":\"User1\"}]";
    const char* deliveryJson = "[]";
    std::string label;
    // 123456 in hex is 1E240. Keypad entry of 123456 scanned via Wiegand will produce "1E240".
    AccessType result = AccessControl::evaluate("1E240", ownerJson, deliveryJson, &label);
    
    TEST_ASSERT_EQUAL(static_cast<int>(AccessType::OPEN_MAIL), static_cast<int>(result));
    TEST_ASSERT_EQUAL_STRING("User1", label.c_str());
}

void test_access_hex_stored_matches_scanned_hex(void) {
    const char* ownerJson = "[]";
    const char* deliveryJson = "[{\"code\":\"1E240\",\"label\":\"DHL\"}]";
    std::string label;
    // Stored code is "1E240", scanned code is "1E240". Should match exactly.
    AccessType result = AccessControl::evaluate("1E240", ownerJson, deliveryJson, &label);
    
    TEST_ASSERT_EQUAL(static_cast<int>(AccessType::OPEN_PARCEL), static_cast<int>(result));
    TEST_ASSERT_EQUAL_STRING("DHL", label.c_str());
}

void test_access_case_insensitive_hex_match(void) {
    const char* ownerJson = "[{\"code\":\"abcdef\",\"label\":\"OwnerLower\"}]";
    const char* deliveryJson = "[{\"code\":\"1e240\",\"label\":\"DeliveryLower\"}]";
    std::string label;

    AccessType ownerRes = AccessControl::evaluate("ABCDEF", ownerJson, deliveryJson, &label);
    TEST_ASSERT_EQUAL(static_cast<int>(AccessType::OPEN_MAIL), static_cast<int>(ownerRes));
    TEST_ASSERT_EQUAL_STRING("OwnerLower", label.c_str());

    AccessType deliveryRes = AccessControl::evaluate("1E240", ownerJson, deliveryJson, &label);
    TEST_ASSERT_EQUAL(static_cast<int>(AccessType::OPEN_PARCEL), static_cast<int>(deliveryRes));
    TEST_ASSERT_EQUAL_STRING("DeliveryLower", label.c_str());
}

void test_access_keypad_burst_mode_does_not_convert_decimal_pin_as_hex(void) {
    const char* ownerJson = "[{\"code\":\"16\",\"label\":\"User16\"}]";
    const char* deliveryJson = "[{\"code\":\"10\",\"label\":\"Courier10\"}]";
    std::string label;

    // In 4-bit or 8-bit keypad mode, PIN "10" is decimal "10", NOT hex 0x10 (16).
    AccessType res4Bit = AccessControl::evaluate("10", ownerJson, deliveryJson, &label, 4);
    TEST_ASSERT_EQUAL(static_cast<int>(AccessType::OPEN_PARCEL), static_cast<int>(res4Bit));
    TEST_ASSERT_EQUAL_STRING("Courier10", label.c_str());

    AccessType res8Bit = AccessControl::evaluate("10", ownerJson, "[]", &label, 8);
    TEST_ASSERT_EQUAL(static_cast<int>(AccessType::DENIED), static_cast<int>(res8Bit));
}

void test_access_invalid_hex_does_not_match_zero(void) {
    const char* ownerJson = "[{\"code\":\"0\",\"label\":\"ZeroUser\"}]";
    AccessType result = AccessControl::evaluate("NOT_HEX", ownerJson, "[]");
    TEST_ASSERT_EQUAL(static_cast<int>(AccessType::DENIED), static_cast<int>(result));
}

void test_access_26bit_matches_decimal_with_leading_zero(void) {
    // On a 26-bit keypad (e.g. Gelikom BP3), entering PIN "042156" transmits integer 42156 -> hex "A4AC".
    const char* deliveryJson = "[{\"code\":\"042156\",\"label\":\"DHL_Zero\"}]";
    std::string label;
    AccessType res26 = AccessControl::evaluate("A4AC", "[]", deliveryJson, &label, 26);
    TEST_ASSERT_EQUAL(static_cast<int>(AccessType::OPEN_PARCEL), static_cast<int>(res26));
    TEST_ASSERT_EQUAL_STRING("DHL_Zero", label.c_str());

    // In 4-bit keypad mode, leading zeros must still match exactly ("42156" != "042156").
    AccessType res4 = AccessControl::evaluate("42156", "[]", deliveryJson, &label, 4);
    TEST_ASSERT_EQUAL(static_cast<int>(AccessType::DENIED), static_cast<int>(res4));

    // All-zero stored code "0000" matches hex "0" in 26-bit mode, but not "1".
    const char* zeroJson = "[{\"code\":\"0000\",\"label\":\"AllZero\"}]";
    TEST_ASSERT_EQUAL(static_cast<int>(AccessType::OPEN_MAIL), static_cast<int>(AccessControl::evaluate("0", zeroJson, "[]", &label, 26)));
    TEST_ASSERT_EQUAL(static_cast<int>(AccessType::DENIED), static_cast<int>(AccessControl::evaluate("1", zeroJson, "[]", &label, 26)));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_access_denied_empty_json);
    RUN_TEST(test_access_open_mail_owner);
    RUN_TEST(test_access_open_parcel_delivery);
    RUN_TEST(test_access_denied_wrong_code);
    RUN_TEST(test_access_handles_broken_json);
    RUN_TEST(test_access_handles_missing_label);
    RUN_TEST(test_access_decimal_stored_matches_scanned_hex);
    RUN_TEST(test_access_hex_stored_matches_scanned_hex);
    RUN_TEST(test_access_case_insensitive_hex_match);
    RUN_TEST(test_access_keypad_burst_mode_does_not_convert_decimal_pin_as_hex);
    RUN_TEST(test_access_invalid_hex_does_not_match_zero);
    RUN_TEST(test_access_26bit_matches_decimal_with_leading_zero);
    UNITY_END();
    return 0;
}
