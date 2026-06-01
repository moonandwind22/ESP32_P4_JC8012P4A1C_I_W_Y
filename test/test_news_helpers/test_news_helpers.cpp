#include <Arduino.h>
#include <unity.h>

#include "../../src/news_service.cpp"

void test_news_markup_helpers_extract_items()
{
    const String payload =
        "<?xml version=\"1.0\"?>"
        "<rss><channel>"
        "<item><title>First &amp; Headline</title><description><![CDATA[<p>Lead <b>story</b></p>]]></description><link>https://example.com/1</link></item>"
        "<item><title>Second Headline</title><description>Another summary</description><link>https://example.com/2</link></item>"
        "</channel></rss>";

    int search_from = 0;
    String block;
    TEST_ASSERT_TRUE(extract_item_block(payload, &search_from, &block));
    TEST_ASSERT_EQUAL_STRING("First & Headline", extract_tag_value(block, "title").c_str());
    TEST_ASSERT_EQUAL_STRING("Lead story", extract_tag_value(block, "description").c_str());
    TEST_ASSERT_EQUAL_STRING("https://example.com/1", extract_tag_value(block, "link").c_str());

    TEST_ASSERT_TRUE(extract_item_block(payload, &search_from, &block));
    TEST_ASSERT_EQUAL_STRING("Second Headline", extract_tag_value(block, "title").c_str());
}

void setup()
{
    delay(200);
    UNITY_BEGIN();
    RUN_TEST(test_news_markup_helpers_extract_items);
    UNITY_END();
}

void loop() {}
