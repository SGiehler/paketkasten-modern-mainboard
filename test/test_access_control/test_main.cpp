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

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_access_denied_empty_json);
    RUN_TEST(test_access_open_mail_owner);
    RUN_TEST(test_access_open_parcel_delivery);
    RUN_TEST(test_access_denied_wrong_code);
    RUN_TEST(test_access_handles_broken_json);
    RUN_TEST(test_access_handles_missing_label);
    UNITY_END();
    return 0;
}
