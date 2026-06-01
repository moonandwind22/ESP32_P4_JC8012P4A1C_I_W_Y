#include <Arduino.h>
#include <unity.h>

#include "runtime_helpers.h"

void test_wifi_credentials_require_both_values()
{
    TEST_ASSERT_FALSE(londonbrief_wifi_credentials_configured("YOUR_WIFI_SSID", "secret"));
    TEST_ASSERT_FALSE(londonbrief_wifi_credentials_configured("HomeWiFi", "YOUR_WIFI_PASSWORD"));
    TEST_ASSERT_FALSE(londonbrief_wifi_credentials_configured("", "secret"));
    TEST_ASSERT_FALSE(londonbrief_wifi_credentials_configured("HomeWiFi", ""));
    TEST_ASSERT_TRUE(londonbrief_wifi_credentials_configured("HomeWiFi", "secret"));
}

void test_runtime_wifi_text_prefers_status_text()
{
    TEST_ASSERT_EQUAL_STRING(
        "Waiting for hosted link",
        londonbrief_runtime_wifi_text(true, false, "Waiting for hosted link")
    );
    TEST_ASSERT_EQUAL_STRING("Connected", londonbrief_runtime_wifi_text(true, true, ""));
    TEST_ASSERT_EQUAL_STRING("Reconnecting", londonbrief_runtime_wifi_text(true, false, ""));
    TEST_ASSERT_EQUAL_STRING("Not configured", londonbrief_runtime_wifi_text(false, false, ""));
}

void setup()
{
    delay(200);
    UNITY_BEGIN();
    RUN_TEST(test_wifi_credentials_require_both_values);
    RUN_TEST(test_runtime_wifi_text_prefers_status_text);
    UNITY_END();
}

void loop() {}
