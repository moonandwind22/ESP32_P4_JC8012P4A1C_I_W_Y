#include <Arduino.h>
#include <unity.h>

#include "../../main/weather_service.cpp"

void test_weather_code_text_and_labels()
{
    TEST_ASSERT_EQUAL_STRING("Clear", weather_code_to_text(0, true));
    TEST_ASSERT_EQUAL_STRING("Clear night", weather_code_to_text(0, false));

    char label[8] = {};
    format_hour_label("2026-05-07T14:00", label, sizeof(label));
    TEST_ASSERT_EQUAL_STRING("14:00", label);
}

void test_weather_hour_index_and_rain_outlook()
{
    JsonDocument doc;
    JsonArray times = doc.to<JsonArray>();
    times.add("2026-05-07T08:00");
    times.add("2026-05-07T09:00");
    times.add("2026-05-07T10:00");

    TEST_ASSERT_EQUAL_UINT32(1, find_hourly_start_index(times, "2026-05-07T09:00"));
    TEST_ASSERT_EQUAL_UINT32(2, find_hourly_start_index(times, "2026-05-07T09:30"));

    WeatherData data = {};
    data.hourly_count = 3;
    snprintf(data.hourly_labels[0], sizeof(data.hourly_labels[0]), "%s", "09:00");
    snprintf(data.hourly_labels[1], sizeof(data.hourly_labels[1]), "%s", "10:00");
    snprintf(data.hourly_labels[2], sizeof(data.hourly_labels[2]), "%s", "11:00");
    data.hourly_precipitation[0] = 10;
    data.hourly_precipitation[1] = 55;
    data.hourly_precipitation[2] = 20;

    char outlook[72] = {};
    build_rain_outlook(data, outlook, sizeof(outlook));
    TEST_ASSERT_EQUAL_STRING("Rain likely 10:00 (55%)", outlook);
}

void setup()
{
    delay(200);
    UNITY_BEGIN();
    RUN_TEST(test_weather_code_text_and_labels);
    RUN_TEST(test_weather_hour_index_and_rain_outlook);
    UNITY_END();
}

void loop() {}
