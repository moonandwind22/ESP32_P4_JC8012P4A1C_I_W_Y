#include <Arduino.h>
#include <unity.h>

#include "../../src/tfl_service.cpp"

void test_tfl_ignore_case_compare_and_deduplicate()
{
    TEST_ASSERT_TRUE(text_equals_ignore_case("Elizabeth line", "elizabeth LINE"));
    TEST_ASSERT_LESS_THAN(0, compare_text_ignore_case("Bakerloo", "Central"));

    TflData data = {};

    RawLineStatus first = {};
    snprintf(first.name, sizeof(first.name), "%s", "Elizabeth line");
    snprintf(first.status, sizeof(first.status), "%s", "Good Service");
    first.severity = 10;
    first.disrupted = false;

    RawLineStatus duplicate = {};
    snprintf(duplicate.name, sizeof(duplicate.name), "%s", "ELIZABETH LINE");
    snprintf(duplicate.status, sizeof(duplicate.status), "%s", "Severe Delays");
    duplicate.severity = 1;
    duplicate.disrupted = true;

    push_line(data, first);
    push_line(data, duplicate);

    TEST_ASSERT_EQUAL_UINT8(1, data.line_count);
    TEST_ASSERT_EQUAL_STRING("Elizabeth line", data.lines[0].name);
    TEST_ASSERT_EQUAL_STRING("Good Service", data.lines[0].status);
}

void setup()
{
    delay(200);
    UNITY_BEGIN();
    RUN_TEST(test_tfl_ignore_case_compare_and_deduplicate);
    UNITY_END();
}

void loop() {}
