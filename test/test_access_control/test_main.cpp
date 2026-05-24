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

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_access_denied_empty_json);
    RUN_TEST(test_access_open_mail_owner);
    RUN_TEST(test_access_open_parcel_delivery);
    RUN_TEST(test_access_denied_wrong_code);
    RUN_TEST(test_access_handles_broken_json);
    RUN_TEST(test_access_handles_missing_label);
    RUN_TEST(test_access_decimal_stored_matches_scanned_hex);
    RUN_TEST(test_access_hex_stored_matches_scanned_hex);
    UNITY_END();
    return 0;
}
