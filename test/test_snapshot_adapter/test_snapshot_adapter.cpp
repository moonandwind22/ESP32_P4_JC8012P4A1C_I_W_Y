#include <Arduino.h>
#include <unity.h>

#include "dashboard_snapshot_adapter.h"

#include "../../main/dashboard_snapshot_adapter.cpp"

namespace {

void copy_text(char *dest, size_t dest_size, const char *src)
{
    snprintf(dest, dest_size, "%s", src);
}

}  // namespace

void test_snapshot_adapter_maps_local_service_state()
{
    DashboardSystemContext system = {};
    system.wifi_configured = true;
    system.wifi_connected = true;
    system.wifi_ready = true;
    system.network_mode = brief::kNetworkModeAutomatic;
    system.brightness_percent = 42;
    system.wifi_status_text = "Wi-Fi connected";

    WeatherData weather = {};
    weather.valid = true;
    copy_text(weather.temperature, sizeof(weather.temperature), "12");
    copy_text(weather.condition, sizeof(weather.condition), "Cloudy");
    copy_text(weather.detail, sizeof(weather.detail), "Feels 11C");
    copy_text(weather.updated, sizeof(weather.updated), "09:30");
    weather.humidity_percent = 71;
    weather.hourly_count = 1;
    copy_text(weather.hourly_labels[0], sizeof(weather.hourly_labels[0]), "10:00");
    weather.hourly_temperatures[0] = 13;

    TflData tfl = {};
    tfl.valid = true;
    tfl.line_count = 1;
    copy_text(tfl.summary, sizeof(tfl.summary), "All clear");
    copy_text(tfl.updated, sizeof(tfl.updated), "09:31");
    tfl.lines[0].available = true;
    copy_text(tfl.lines[0].name, sizeof(tfl.lines[0].name), "Elizabeth line");
    copy_text(tfl.lines[0].status, sizeof(tfl.lines[0].status), "Good Service");

    NewsData news = {};
    news.valid = true;
    news.headline_count = 1;
    copy_text(news.updated, sizeof(news.updated), "09:32");
    copy_text(news.headlines[0], sizeof(news.headlines[0]), "Morning briefing");
    copy_text(news.summaries[0], sizeof(news.summaries[0]), "A compact summary.");

    brief::DashboardSnapshot snapshot = {};
    build_dashboard_snapshot(system, weather, tfl, news, &snapshot);

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(brief::kMsgDashboardSnapshot), snapshot.header.type);
    TEST_ASSERT_EQUAL_UINT8(1, snapshot.system.wifi_connected);
    TEST_ASSERT_EQUAL_STRING("Wi-Fi connected", snapshot.system.status_text);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(brief::kDataStateLive), snapshot.weather.now.state);
    TEST_ASSERT_EQUAL_INT16(120, snapshot.weather.now.temperature_tenths_c);
    TEST_ASSERT_EQUAL_STRING("Cloudy", snapshot.weather.now.condition);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(brief::kDataStateLive), snapshot.tfl.state);
    TEST_ASSERT_EQUAL_STRING("Elizabeth line", snapshot.tfl.lines[0].line_name);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(brief::kDataStateLive), snapshot.news.state);
    TEST_ASSERT_EQUAL_STRING("Morning briefing", snapshot.news.headlines[0].title);
}

void setup()
{
    delay(200);
    UNITY_BEGIN();
    RUN_TEST(test_snapshot_adapter_maps_local_service_state);
    UNITY_END();
}

void loop() {}
